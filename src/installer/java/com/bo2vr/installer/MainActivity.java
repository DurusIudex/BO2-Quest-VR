package com.bo2vr.installer;

import android.app.*;
import android.content.*;
import android.content.pm.*;
import android.graphics.Color;
import android.net.Uri;
import android.os.*;
import android.provider.Settings;
import android.view.*;
import android.widget.*;
import org.json.*;
import java.io.*;
import java.net.*;
import java.security.MessageDigest;
import java.util.concurrent.Executors;
import java.util.concurrent.ExecutorService;

public final class MainActivity extends Activity {
    private TextView status,versions;
    private ProgressBar progress;
    private Button check,install,play,self;
    private volatile boolean busy;
    private JSONObject feed;
    private final ExecutorService worker=Executors.newSingleThreadExecutor();
    private android.content.SharedPreferences prefs(){return getSharedPreferences("launcher",0);}
    private int dp(int n){return (int)(n*getResources().getDisplayMetrics().density+.5f);}
    private TextView text(LinearLayout parent,String value,int size,int color){
        TextView t=new TextView(this);t.setText(value);t.setTextSize(size);t.setTextColor(color);t.setPadding(0,dp(6),0,dp(6));parent.addView(t);return t;
    }
    private Button button(LinearLayout p,String label,Runnable action){Button b=new Button(this);b.setText(label);p.addView(b,new LinearLayout.LayoutParams(-1,dp(54)));b.setOnClickListener(v->action.run());return b;}
    @Override public void onCreate(Bundle state){
        super.onCreate(state);
        ScrollView scroll=new ScrollView(this);scroll.setFillViewport(true);scroll.setBackgroundColor(Color.rgb(14,19,27));
        LinearLayout content=new LinearLayout(this);content.setOrientation(1);content.setPadding(dp(28),dp(18),dp(28),dp(22));scroll.addView(content);setContentView(scroll);
        text(content,"BO2 / QUEST VR",30,Color.WHITE);
        text(content,"Install once. Keep your VR build up to date.",18,Color.rgb(145,198,218));
        versions=text(content,"",15,Color.LTGRAY);
        status=text(content,"Checking GitHub releases…",18,Color.WHITE);
        progress=new ProgressBar(this,null,android.R.attr.progressBarStyleHorizontal);progress.setMax(100);content.addView(progress,new LinearLayout.LayoutParams(-1,dp(10)));
        button(content,"Install Zombies + Multiplayer files from PC",()->startActivity(new Intent(this,GameDataActivity.class)));
        install=button(content,"Install / update VR app",()->download(false));install.setEnabled(false);
        play=button(content,"Launch game",this::launch);
        check=button(content,"Check for updates",()->checkRelease(false));
        self=button(content,"Update launcher",()->download(true));self.setVisibility(View.GONE);
        Switch automatic=new Switch(this);automatic.setText("Download game updates when launcher opens");automatic.setTextColor(Color.WHITE);automatic.setPadding(0,dp(12),0,dp(12));automatic.setChecked(prefs().getBoolean("automatic",true));content.addView(automatic);
        automatic.setOnCheckedChangeListener((v,on)->prefs().edit().putBoolean("automatic",on).apply());
        text(content,"Install both game modes from your owned Steam copy using the PC transfer button above. The VR app and its updates download from GitHub. After setup, gameplay is standalone. Existing game files and controller settings are kept.",15,Color.LTGRAY);
        text(content,"Experimental build • Quest may ask you to allow installations and confirm an update.",14,Color.GRAY);
        refreshInstalled();
        if(!prefs().contains("pending"))checkRelease(true);
    }
    @Override public void onNewIntent(Intent intent){super.onNewIntent(intent);setIntent(intent);if(intent.getBooleanExtra("data_ready",false)&&!busy){if(installed(ReleasePolicy.GAME)>0)launch();else if(feed!=null)download(false);else checkRelease(true);}}
    @Override public void onResume(){
        super.onResume();refreshInstalled();
        String result=prefs().getString("status",null);if(result!=null){status.setText(result);prefs().edit().remove("status").apply();}
        if(!busy&&prefs().contains("pending")){
            if(getPackageManager().canRequestPackageInstalls()){
                busy=true;updateButtons();worker.execute(()->{try{
                    JSONObject artifact=new JSONObject(prefs().getString("pendingArtifact","{}"));
                    File apk=new File(prefs().getString("pending",""));
                    validate(apk,artifact);commit(apk,artifact);
                }catch(Exception e){fail(e);}finally{busy=false;runOnUiThread(this::updateButtons);}});
            }else status.setText("Allow BO2 Quest Launcher to install apps, then return here. Tap Install / update to reopen settings.");
        }
    }
    private long installed(String pkg){try{return getPackageManager().getPackageInfo(pkg,0).getLongVersionCode();}catch(PackageManager.NameNotFoundException e){return 0;}}
    private void refreshInstalled(){if(versions==null)return;long v=installed(ReleasePolicy.GAME);versions.setText(v==0?"Game app not installed":"Installed game build: "+v);play.setEnabled(v>0);}
    private void updateButtons(){if(isDestroyed())return;check.setEnabled(!busy);install.setEnabled(!busy&&feed!=null);self.setEnabled(!busy);play.setEnabled(!busy&&installed(ReleasePolicy.GAME)>0);}
    private void show(String s){runOnUiThread(()->{if(!isDestroyed())status.setText(s);});}
    private void fail(Exception e){prefs().edit().remove("pending").remove("pendingArtifact").apply();android.util.Log.w("BO2Installer","Update failed",e);show("Update unavailable: "+e.getMessage()+". Your installed game is still available.");}
    private HttpURLConnection connect(String url)throws Exception{
        for(int i=0;i<6;i++){
            if(!ReleasePolicy.allowedDownload(url))throw new IOException("Untrusted download address");
            HttpURLConnection c=(HttpURLConnection)new URL(url).openConnection();c.setInstanceFollowRedirects(false);c.setConnectTimeout(20000);c.setReadTimeout(30000);c.setRequestProperty("User-Agent","BO2QuestLauncher/1");c.setRequestProperty("Cache-Control","no-cache");
            int code=c.getResponseCode();
            if(code>=300&&code<400){String target=c.getHeaderField("Location");c.disconnect();if(target==null)throw new IOException("Missing redirect");url=new URL(new URL(url),target).toString();continue;}
            if(code!=200){c.disconnect();throw new IOException("GitHub returned "+code);}
            return c;
        }throw new IOException("Too many redirects");
    }
    private JSONObject artifact(boolean launcher)throws Exception{
        if(feed==null||feed.getInt("schema")!=1)throw new IOException("Unsupported update feed");
        JSONObject a=feed.getJSONObject(launcher?"launcher":"game");
        ReleasePolicy.requireArtifact(a.getString("package"),a.getLong("versionCode"),a.getLong("size"),a.getString("sha256"),a.getString("url"));
        if(!a.getString("package").equals(launcher?ReleasePolicy.LAUNCHER:ReleasePolicy.GAME))throw new IOException("Wrong update package");
        return a;
    }
    private void checkRelease(boolean auto){
        if(busy)return;busy=true;updateButtons();show("Checking GitHub releases…");
        worker.execute(()->{boolean shouldDownload=false;try{
            HttpURLConnection c=connect(ReleasePolicy.FEED);ByteArrayOutputStream out=new ByteArrayOutputStream();
            try(InputStream in=c.getInputStream()){byte[] b=new byte[4096];int n;while((n=in.read(b))!=-1){if(out.size()+n>65536)throw new IOException("Update feed too large");out.write(b,0,n);}}finally{c.disconnect();}
            feed=new JSONObject(out.toString("UTF-8"));JSONObject game=artifact(false),launcher=artifact(true);
            long current=installed(ReleasePolicy.GAME);boolean newer=ReleasePolicy.newer(game.getLong("versionCode"),current);
            shouldDownload=auto&&newer&&((current>0&&prefs().getBoolean("automatic",true))||getIntent().getBooleanExtra("data_ready",false));
            final String message=newer?(current==0?"Ready to install ":"Update available: ")+game.getString("versionName"):"You’re up to date. Ready to play.";
            final boolean updateSelf=ReleasePolicy.newer(launcher.getLong("versionCode"),installed(getPackageName()));
            runOnUiThread(()->{status.setText(message);install.setText("Install / update VR app");self.setVisibility(updateSelf?View.VISIBLE:View.GONE);});
        }catch(Exception e){fail(e);}finally{busy=false;final boolean next=shouldDownload;runOnUiThread(()->{updateButtons();if(next)download(false);});}});
    }
    private void download(boolean launcher){
        if(busy)return;
        if(prefs().contains("pending")&&!getPackageManager().canRequestPackageInstalls()){allowInstalls();return;}
        busy=true;updateButtons();worker.execute(()->{try{
            JSONObject a=artifact(launcher);long version=a.getLong("versionCode");String pkg=a.getString("package");
            if(!ReleasePolicy.newer(version,installed(pkg))){show("Already up to date. Ready to play.");return;}
            File part=new File(getCacheDir(),launcher?"launcher.part":"game.part"),apk=new File(getCacheDir(),launcher?"launcher.apk":"game.apk");
            long expected=a.getLong("size"),total=0;show("Downloading "+a.getString("versionName")+"…");HttpURLConnection c=connect(a.getString("url"));
            try(InputStream in=c.getInputStream();FileOutputStream out=new FileOutputStream(part)){
                byte[] buf=new byte[65536];int n,last=-1;
                while((n=in.read(buf))!=-1){total+=n;if(total>expected)throw new IOException("Download exceeds expected size");out.write(buf,0,n);int percent=(int)(100*total/expected);if(percent!=last){last=percent;runOnUiThread(()->progress.setProgress(percent));}}
                out.getFD().sync();
            }finally{c.disconnect();}
            if(total!=expected)throw new IOException("Download incomplete");
            validate(part,a);if(apk.exists()&&!apk.delete())throw new IOException("Cannot replace cached update");if(!part.renameTo(apk))throw new IOException("Cannot save update");
            prefs().edit().putString("pending",apk.getAbsolutePath()).putString("pendingArtifact",a.toString()).commit();
            if(getPackageManager().canRequestPackageInstalls())commit(apk,a);else runOnUiThread(this::allowInstalls);
        }catch(Exception e){fail(e);}finally{busy=false;runOnUiThread(this::updateButtons);}});
    }
    private void validate(File apk,JSONObject a)throws Exception{
        if(apk.length()!=a.getLong("size"))throw new IOException("Wrong APK size");
        MessageDigest digest=MessageDigest.getInstance("SHA-256");try(InputStream in=new FileInputStream(apk)){byte[] b=new byte[65536];int n;while((n=in.read(b))!=-1)digest.update(b,0,n);}
        if(!ReleasePolicy.hex(digest.digest()).equals(a.getString("sha256")))throw new IOException("APK checksum mismatch");
        PackageInfo p=getPackageManager().getPackageArchiveInfo(apk.getAbsolutePath(),PackageManager.GET_SIGNING_CERTIFICATES);
        if(p==null||!p.packageName.equals(a.getString("package"))||p.getLongVersionCode()!=a.getLong("versionCode"))throw new IOException("APK identity/version mismatch");
        if(p.signingInfo==null||p.signingInfo.getApkContentsSigners().length!=1||!ReleasePolicy.hash(p.signingInfo.getApkContentsSigners()[0].toByteArray()).equals(ReleasePolicy.CERT))throw new IOException("APK signer mismatch");
        if(p.getLongVersionCode()<=installed(p.packageName))throw new IOException("Older or already installed version");
    }
    private void allowInstalls(){show("Allow installations from BO2 Quest Launcher, then return here.");try{startActivity(new Intent(Settings.ACTION_MANAGE_UNKNOWN_APP_SOURCES,Uri.parse("package:"+getPackageName())));}catch(RuntimeException e){show("Open Quest settings and allow installations from BO2 Quest Launcher, then retry.");}}
    private void commit(File apk,JSONObject a)throws Exception{
        show("Installing update… Confirm any Quest installation prompt.");
        PackageInstaller installer=getPackageManager().getPackageInstaller();PackageInstaller.SessionParams params=new PackageInstaller.SessionParams(PackageInstaller.SessionParams.MODE_FULL_INSTALL);
        params.setAppPackageName(a.getString("package"));params.setSize(apk.length());
        if(Build.VERSION.SDK_INT>=31)params.setRequireUserAction(PackageInstaller.SessionParams.USER_ACTION_NOT_REQUIRED);
        int id=installer.createSession(params);
        try(PackageInstaller.Session session=installer.openSession(id)){
            try(InputStream in=new FileInputStream(apk);OutputStream out=session.openWrite("base.apk",0,apk.length())){byte[] b=new byte[65536];int n;while((n=in.read(b))!=-1)out.write(b,0,n);session.fsync(out);}
            Intent callback=new Intent(this,InstallReceiver.class).setAction("com.bo2vr.installer.INSTALL_RESULT");
            PendingIntent pi=PendingIntent.getBroadcast(this,id,callback,PendingIntent.FLAG_UPDATE_CURRENT|PendingIntent.FLAG_MUTABLE);
            session.commit(pi.getIntentSender());prefs().edit().remove("pending").remove("pendingArtifact").apply();
        }catch(Exception e){installer.abandonSession(id);throw e;}
    }
    private void launch(){Intent i=getPackageManager().getLaunchIntentForPackage(ReleasePolicy.GAME);if(i==null){show("Install the game app first.");return;}startActivity(i);}
}
