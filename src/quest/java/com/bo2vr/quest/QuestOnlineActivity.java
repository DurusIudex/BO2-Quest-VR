package com.bo2vr.quest;

import android.app.Activity;
import android.os.Bundle;
import android.content.Intent;
import android.widget.*;
import java.io.*;
import java.net.*;
import java.nio.charset.StandardCharsets;
import java.util.Locale;
import org.json.JSONObject;

public final class QuestOnlineActivity extends Activity {
    private static final String API="https://159-223-145-188.sslip.io/bo2vr/";
    private Spinner mode;
    private EditText code;
    private TextView status;
    private Button host,join,offline;
    private boolean busy;
    @Override public void onCreate(Bundle saved){
        super.onCreate(saved);
        LinearLayout root=new LinearLayout(this);root.setOrientation(1);root.setPadding(32,16,32,16);
        ScrollView scroll=new ScrollView(this);scroll.addView(root);setContentView(scroll);
        TextView title=new TextView(this);title.setText("Online rooms");title.setTextSize(26);root.addView(title);
        TextView help=new TextView(this);help.setText("Experimental online rooms. Host: choose a code, select your map in Private Match, and wait for guests before starting. Guests: select the same mode and enter the host’s code. The host must remain in the game. Full game-to-game joining still needs testing.");root.addView(help);
        mode=new Spinner(this);mode.setAdapter(new ArrayAdapter<String>(this,android.R.layout.simple_spinner_dropdown_item,new String[]{"Zombies","Multiplayer"}));root.addView(mode);
        code=new EditText(this);code.setSingleLine(true);code.setHint("Room code (6–24 letters, numbers or hyphens)");code.setInputType(1|4096);root.addView(code);
        host=button(root,"Host with this code",()->connect("create"));
        join=button(root,"Join with this code",()->connect("join"));
        offline=button(root,"Play offline",()->{if(!busy){new File(getFilesDir(),"quest_online.ini").delete();launch(selectedMode());}});
        status=new TextView(this);status.setTextSize(17);root.addView(status);
        button(root,"Back",()->finish());
    }
    private Button button(LinearLayout root,String text,Runnable action){Button b=new Button(this);b.setText(text);b.setOnClickListener(v->action.run());root.addView(b);return b;}
    private String selectedMode(){return mode.getSelectedItemPosition()==0?"zm":"mp";}
    private void setBusy(boolean value){busy=value;host.setEnabled(!value);join.setEnabled(!value);offline.setEnabled(!value);mode.setEnabled(!value);code.setEnabled(!value);}
    private void connect(String action){
        if(busy)return;
        final String room=code.getText().toString().trim().toUpperCase(Locale.ROOT),game=selectedMode();
        if(!room.matches("[A-Z0-9-]{6,24}")){status.setText("Use 6–24 letters, numbers or hyphens.");return;}
        setBusy(true);status.setText(action.equals("create")?"Creating room…":"Joining room…");
        new Thread(()->{
            try{
                JSONObject request=new JSONObject();request.put("code",room);request.put("mode",game);
                JSONObject response=post(action,request);
                String token=response.getString("token"),address=response.getString("host");
                int peer=response.getInt("peer"),port=response.getInt("port");
                if(!token.matches("[0-9a-f]{32}")||!address.matches("[0-9.]{7,15}")||peer<1||peer>18||port<1||port>65535)throw new IOException("Invalid relay response.");
                String text="version=1\nhost="+address+"\nport="+port+"\npeer="+peer+"\ntoken="+token+"\nmode="+game+"\n";
                File target=new File(getFilesDir(),"quest_online.ini"),temp=new File(getFilesDir(),"quest_online.ini.tmp");
                try(FileOutputStream out=new FileOutputStream(temp)){out.write(text.getBytes(StandardCharsets.UTF_8));out.getFD().sync();}
                if(!temp.renameTo(target))throw new IOException("Could not save room connection.");
                runOnUiThread(()->{if(!isFinishing()){setBusy(false);status.setText("Room ready. Opening game…");launch(game);}});
            }catch(Exception e){runOnUiThread(()->{if(!isFinishing()){setBusy(false);status.setText(e.getMessage()==null?"Connection failed.":e.getMessage());}});}
        },"bo2-room").start();
    }
    private static JSONObject post(String action,JSONObject body)throws Exception{
        HttpURLConnection connection=(HttpURLConnection)new URL(API+action).openConnection();
        connection.setConnectTimeout(10000);connection.setReadTimeout(10000);connection.setRequestMethod("POST");connection.setDoOutput(true);
        connection.setRequestProperty("Content-Type","application/json");connection.setInstanceFollowRedirects(false);
        byte[] bytes=body.toString().getBytes(StandardCharsets.UTF_8);connection.setFixedLengthStreamingMode(bytes.length);
        try{
            try(OutputStream out=connection.getOutputStream()){out.write(bytes);}
            int result=connection.getResponseCode();InputStream stream=result==200?connection.getInputStream():connection.getErrorStream();
            if(stream==null)throw new IOException("Relay unavailable ("+result+").");
            ByteArrayOutputStream data=new ByteArrayOutputStream();byte[] chunk=new byte[1024];int read;
            try(InputStream in=stream){while((read=in.read(chunk))!=-1){if(data.size()+read>8192)throw new IOException("Invalid relay response.");data.write(chunk,0,read);}}
            JSONObject response=new JSONObject(new String(data.toByteArray(),StandardCharsets.UTF_8));
            if(result!=200)throw new IOException(response.optString("error","Relay request failed ("+result+")."));
            return response;
        }finally{connection.disconnect();}
    }
    private void launch(String game){
        Intent intent=new Intent();intent.setClassName(getPackageName(),"org.wiiucoop.bo2android.MenuActivity");
        intent.putExtra("quest_mode",game);intent.addFlags(Intent.FLAG_ACTIVITY_CLEAR_TOP);startActivity(intent);finish();
    }
}
