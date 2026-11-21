package com.bo2vr.quest;
import android.app.Activity;
import android.content.Intent;
import android.view.ViewGroup;
import android.widget.Button;
import android.widget.FrameLayout;
import android.view.Gravity;

public final class QuestBridge {
    static final String[] RESOLUTIONS={"1280x720","1920x1080"};
    static String resolution(java.util.Properties settings){
        String value=settings.getProperty("resolution","1920x1080");
        for(String allowed:RESOLUTIONS)if(allowed.equals(value))return value;
        return "1920x1080";
    }
    static void applyResolution(Activity activity){
        java.util.Properties settings=new java.util.Properties();
        try(java.io.FileInputStream in=new java.io.FileInputStream(new java.io.File(activity.getFilesDir(),"quest_bindings.ini"))){settings.load(in);}catch(java.io.IOException ignored){}
        activity.getSharedPreferences("t6_ui",4).edit().putString("resolution",resolution(settings)).commit();
    }
    public static void addSettings(Activity activity){
        applyResolution(activity);
        activity.getSharedPreferences("t6_ui",4).edit().putString("touch_mode","off").commit();
        FrameLayout root=activity.findViewById(android.R.id.content);
        Button button=new Button(activity);button.setText("Quest settings");
        FrameLayout.LayoutParams layout=new FrameLayout.LayoutParams(ViewGroup.LayoutParams.WRAP_CONTENT,ViewGroup.LayoutParams.WRAP_CONTENT,Gravity.BOTTOM|Gravity.END);
        layout.setMargins(12,12,20,12);root.addView(button,layout);
        button.setOnClickListener(v->activity.startActivity(new Intent(activity,QuestSettingsActivity.class)));
        Button online=new Button(activity);online.setText("Online rooms");
        FrameLayout.LayoutParams onlineLayout=new FrameLayout.LayoutParams(ViewGroup.LayoutParams.WRAP_CONTENT,ViewGroup.LayoutParams.WRAP_CONTENT,Gravity.BOTTOM|Gravity.START);
        onlineLayout.setMargins(20,12,12,12);root.addView(online,onlineLayout);
        online.setOnClickListener(v->activity.startActivity(new Intent(activity,QuestOnlineActivity.class)));
        String mode=activity.getIntent().getStringExtra("quest_mode");
        if(("zm".equals(mode)||"mp".equals(mode))&&activity.getClass().getName().endsWith("MenuActivity")){
            activity.getIntent().removeExtra("quest_mode");
            root.postDelayed(()->{
                try{
                    java.lang.reflect.Method select=activity.getClass().getDeclaredMethod("selectMode",String.class);select.setAccessible(true);select.invoke(activity,mode);
                    java.lang.reflect.Method start=activity.getClass().getDeclaredMethod("startSelectedClient");start.setAccessible(true);start.invoke(activity);
                }catch(Exception e){android.util.Log.e("BO2QuestVR","Mode launch failed",e);}
            },800);
        }
    }
    public static void start(Activity activity){
        activity.getSharedPreferences("t6_ui",4).edit().putString("touch_mode","off").putString("gyro_mode","off").putBoolean("gyro_enabled",false).commit();
        System.loadLibrary("questvr");
        nativeStart(activity,activity.getFilesDir().getAbsolutePath());
        // Private developer command inbox allows unattended smoke tests. Only
        // this application's UID (or authorized adb run-as) can write it.
        android.os.Handler handler=new android.os.Handler(android.os.Looper.getMainLooper());
        handler.postDelayed(new Runnable(){public void run(){
            if(activity.isFinishing())return;
            java.io.File command=new java.io.File(activity.getFilesDir(),"quest_command.txt");
            if(command.exists())try{
                byte[] data=java.nio.file.Files.readAllBytes(command.toPath());
                if(data.length<=1024){
                    Class<?> nativeClass=Class.forName("org.wiiucoop.bo2android.T6Native");
                    Object result=nativeClass.getMethod("sendCommand",String.class).invoke(null,new String(data,java.nio.charset.StandardCharsets.UTF_8).trim());
                    if(Boolean.TRUE.equals(result)){command.delete();android.util.Log.i("BO2QuestVR","Developer command queued");}
                }
            }catch(Exception e){android.util.Log.w("BO2QuestVR","Developer command failed",e);}
            handler.postDelayed(this,1000);
        }},2000);
    }
    private static native void nativeStart(Activity activity,String files);
}
