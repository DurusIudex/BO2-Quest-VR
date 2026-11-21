package com.bo2vr.installer;

import android.app.*;
import android.content.*;
import android.graphics.Color;
import android.net.Uri;
import android.os.*;
import android.provider.Settings;
import android.view.*;
import android.widget.*;
import org.json.*;
import java.io.*;
import java.util.*;

public final class GameDataActivity extends Activity {
    private EditText address,code;private TextView status;private ProgressBar progress;private Button transfer,next;
    private volatile boolean busy,cancelled;private boolean awaitingStorage;
    private int dp(int n){return (int)(n*getResources().getDisplayMetrics().density+.5f);}
    private TextView text(LinearLayout parent,String value,int size){TextView t=new TextView(this);t.setText(value);t.setTextSize(size);t.setTextColor(Color.WHITE);t.setPadding(0,dp(8),0,dp(8));parent.addView(t);return t;}
    private EditText input(LinearLayout p,String hint){EditText e=new EditText(this);e.setTextColor(Color.WHITE);e.setHintTextColor(Color.LTGRAY);e.setHint(hint);e.setSingleLine(true);p.addView(e,new LinearLayout.LayoutParams(-1,dp(56)));return e;}
    @Override public void onCreate(Bundle state){
        super.onCreate(state);getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
        ScrollView scroll=new ScrollView(this);scroll.setBackgroundColor(Color.rgb(14,19,27));LinearLayout p=new LinearLayout(this);p.setOrientation(1);p.setPadding(dp(24),dp(18),dp(24),dp(24));scroll.addView(p);setContentView(scroll);
        text(p,"INSTALL ZOMBIES + MULTIPLAYER",25);
        text(p,"On your PC, open Start-BO2-Transfer.cmd. Keep the helper running and connect the Quest to the same Wi-Fi. Enter the code displayed on your PC.",17);
        address=input(p,"PC IPv4 address (optional: leave blank to find it)");address.setText(getSharedPreferences("launcher",0).getString("pc_address",""));
        code=input(p,"Pairing code, e.g. ABCD-EFGH-JKLM");code.setInputType(android.text.InputType.TYPE_CLASS_TEXT|android.text.InputType.TYPE_TEXT_FLAG_CAP_CHARACTERS|android.text.InputType.TYPE_TEXT_FLAG_NO_SUGGESTIONS);
        status=text(p,"Both modes will be installed into /storage/emulated/0/BO2. Existing saves and settings are kept.",17);
        progress=new ProgressBar(this,null,android.R.attr.progressBarStyleHorizontal);p.addView(progress,new LinearLayout.LayoutParams(-1,dp(12)));progress.setMax(1000);
        transfer=new Button(this);transfer.setText("Connect and install game files");p.addView(transfer,new LinearLayout.LayoutParams(-1,dp(56)));transfer.setOnClickListener(v->{if(busy){cancelled=true;status.setText("Stopping safely… Partial files can be resumed.");}else begin();});
        next=new Button(this);next.setText("Continue to game setup");next.setVisibility(View.GONE);p.addView(next,new LinearLayout.LayoutParams(-1,dp(56)));next.setOnClickListener(v->{startActivity(new Intent(this,MainActivity.class).putExtra("data_ready",true).addFlags(Intent.FLAG_ACTIVITY_CLEAR_TOP));finish();});
        text(p,"The PC is needed only for this transfer. Keep this screen open until it finishes. If interrupted, reconnect: verified files are skipped and partial downloads resume. Allow file access when Quest asks so both game modes can use the installed folder.",15);
    }
    private boolean storageAllowed(){return Build.VERSION.SDK_INT<30||Environment.isExternalStorageManager();}
    private void begin(){
        if(busy)return;
        try{TransferClient.normalizeCode(code.getText().toString());}catch(Exception e){status.setText(e.getMessage());return;}
        if(!storageAllowed()){
            awaitingStorage=true;status.setText("Allow BO2 Quest Launcher to manage files, then return here to continue.");
            try{startActivity(new Intent(Settings.ACTION_MANAGE_APP_ALL_FILES_ACCESS_PERMISSION,Uri.parse("package:"+getPackageName())));}
            catch(RuntimeException e){try{startActivity(new Intent(Settings.ACTION_MANAGE_ALL_FILES_ACCESS_PERMISSION));}catch(RuntimeException e2){awaitingStorage=false;status.setText("Open Quest settings and allow file access for BO2 Quest Launcher, then retry.");}}
            return;
        }
        final String host=address.getText().toString().trim(),pair=code.getText().toString();getSharedPreferences("launcher",0).edit().putString("pc_address",host).apply();
        busy=true;cancelled=false;transfer.setText("Stop transfer");address.setEnabled(false);code.setEnabled(false);next.setVisibility(View.GONE);status.setText("Finding and pairing with your PC…");
        PowerManager.WakeLock wake=((PowerManager)getSystemService(POWER_SERVICE)).newWakeLock(PowerManager.PARTIAL_WAKE_LOCK,"BO2Quest:GameTransfer");wake.acquire(2*60*60*1000L);
        new Thread(()->{try{
            TransferClient client=TransferClient.pair(host,pair);JSONObject manifest=new JSONObject(new String(client.manifest(),"UTF-8"));
            if(manifest.getInt("schema")!=1||!manifest.getJSONObject("modes").getBoolean("zombies")||!manifest.getJSONObject("modes").getBoolean("multiplayer"))throw new IOException("Install both Zombies and Multiplayer in Steam first");
            JSONArray files=manifest.getJSONArray("files");if(files.length()>10000)throw new IOException("Too many files");List<TransferClient.Entry> entries=new ArrayList<>();Set<String> names=new HashSet<>();long total=0;
            for(int i=0;i<files.length();i++){JSONObject f=files.getJSONObject(i);TransferClient.Entry e=new TransferClient.Entry(f.getString("path"),f.getLong("size"),f.getString("sha256"));entries.add(e);names.add(e.path);total=Math.addExact(total,e.size);}
            if(total!=manifest.getLong("totalBytes")||!names.contains("zone/all/zm_transit.ff")||!names.contains("zone/all/mp_nuketown_2020.ff"))throw new IOException("Incomplete game manifest");
            File root=new File(Environment.getExternalStorageDirectory(),"BO2");
            client.install(root,entries,(name,done,size)->{
                if(cancelled)throw new IOException("Transfer stopped. Reconnect to resume");
                runOnUiThread(()->{if(!isDestroyed()){status.setText(name+"\n"+String.format(Locale.ROOT,"%.2f / %.2f GB",done/1e9,size/1e9));progress.setProgress(size==0?1000:(int)(done*1000/size));}});
            });
            getSharedPreferences("launcher",0).edit().putBoolean("game_data_installed",true).apply();
            runOnUiThread(()->{if(!isDestroyed()){status.setText("Zombies and Multiplayer files installed and verified. In the game's folder selector, choose BO2. The PC can now be turned off.");next.setVisibility(View.VISIBLE);progress.setProgress(1000);}});
        }catch(Exception e){android.util.Log.w("BO2Installer","Game transfer failed",e);runOnUiThread(()->{if(!isDestroyed())status.setText(e.getMessage());});}
        finally{if(wake.isHeld())wake.release();busy=false;runOnUiThread(()->{if(!isDestroyed()){transfer.setText("Connect and install game files");address.setEnabled(true);code.setEnabled(true);}});}},"BO2GameTransfer").start();
    }
    @Override public void onResume(){super.onResume();if(awaitingStorage){awaitingStorage=false;if(storageAllowed())begin();}}
    @Override public void onDestroy(){cancelled=true;super.onDestroy();}
}
