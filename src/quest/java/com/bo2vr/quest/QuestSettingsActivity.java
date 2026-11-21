package com.bo2vr.quest;

import android.app.Activity;
import android.os.Bundle;
import android.view.View;
import android.widget.*;
import java.io.*;
import java.util.Properties;

public final class QuestSettingsActivity extends Activity {
    private static final String[] KEYS={"left_trigger","right_trigger","left_grip","right_grip","x","y","a","b","left_stick","right_stick","menu","left_punch","right_punch"};
    private static final String[] CONTROLS={"Left trigger","Right trigger","Left grip","Right grip","X button","Y button","A button","B button","Left stick click","Right stick click","Left menu button","Left-hand punch","Right-hand punch"};
    private static final String[] ACTIONS={"none","fire","aim","jump","crouch","reload_use","weapon","sprint","melee","grenade","tactical","pause","scores"};
    private static final String[] LABELS={"Unbound","Fire","Aim down sights","Jump","Crouch","Reload / use","Switch weapon","Sprint","Melee","Grenade","Tactical grenade","Pause","Scoreboard"};
    private static final int[] DEFAULTS={2,1,10,9,5,6,3,4,7,8,11,8,8};
    private final Spinner[] choices=new Spinner[KEYS.length];
    private SeekBar deadzone;
    private Switch swap;
    private Spinner rendering;
    private Spinner resolution;
    private Spinner smoothing;
    private Spinner weaponTracking;
    private TextView status;
    private File config() { return new File(getFilesDir(),"quest_bindings.ini"); }
    @Override public void onCreate(Bundle saved) {
        super.onCreate(saved);
        getWindow().getDecorView().setBackgroundColor(0xff111820);
        ScrollView scroll=new ScrollView(this);
        LinearLayout content=new LinearLayout(this); content.setOrientation(1); content.setPadding(32,20,32,28);
        scroll.addView(content); setContentView(scroll);
        TextView heading=new TextView(this); heading.setText("Quest settings"); heading.setTextSize(26); heading.setTextColor(-1); content.addView(heading);
        TextView hint=new TextView(this); hint.setText("Mappings use BO2’s default gamepad layout. Save before starting the game. The Meta button stays reserved for the headset."); hint.setTextColor(0xffb9c8d7); hint.setPadding(0,12,0,18); content.addView(hint);
        Properties values=new Properties(); try(FileInputStream in=new FileInputStream(config())){values.load(in);}catch(IOException ignored){}
        TextView renderLabel=new TextView(this);renderLabel.setText("View mode (restart game after saving)");renderLabel.setTextColor(-1);content.addView(renderLabel);
        rendering=new Spinner(this);rendering.setAdapter(new ArrayAdapter<>(this,android.R.layout.simple_spinner_dropdown_item,new String[]{"Stereo VR (experimental)","Head-tracked mono diagnostic","Stationary game screen"}));
        String render=values.getProperty("render_mode","stereo");rendering.setSelection("mono".equals(render)?1:"panel".equals(render)?2:0);content.addView(rendering);
        TextView resolutionLabel=new TextView(this);resolutionLabel.setText("VR resolution per eye (restart game after saving)");resolutionLabel.setTextColor(-1);content.addView(resolutionLabel);
        resolution=new Spinner(this);resolution.setAdapter(new ArrayAdapter<>(this,android.R.layout.simple_spinner_dropdown_item,new String[]{"Performance: 640 × 720","High: 960 × 1080"}));
        String selectedResolution=QuestBridge.resolution(values);for(int i=0;i<QuestBridge.RESOLUTIONS.length;i++)if(QuestBridge.RESOLUTIONS[i].equals(selectedResolution))resolution.setSelection(i);content.addView(resolution);
        TextView aimLabel=new TextView(this);aimLabel.setText("Aim stabilization (restart game after saving)");aimLabel.setTextColor(-1);content.addView(aimLabel);
        smoothing=new Spinner(this);smoothing.setAdapter(new ArrayAdapter<>(this,android.R.layout.simple_spinner_dropdown_item,new String[]{"Off","Light","Balanced","Strong"}));
        String smooth=values.getProperty("aim_smoothing","balanced");smoothing.setSelection("off".equals(smooth)?0:"light".equals(smooth)?1:"strong".equals(smooth)?3:2);content.addView(smoothing);
        TextView weaponLabel=new TextView(this);weaponLabel.setText("Weapon tracking (restart game after saving)");weaponLabel.setTextColor(-1);content.addView(weaponLabel);
        weaponTracking=new Spinner(this);weaponTracking.setAdapter(new ArrayAdapter<>(this,android.R.layout.simple_spinner_dropdown_item,new String[]{"Position and rotation — physical sights","Rotation only"}));
        weaponTracking.setSelection("rotation".equals(values.getProperty("weapon_tracking"))?1:0);content.addView(weaponTracking);
        for(int i=0;i<KEYS.length;i++){
            LinearLayout row=new LinearLayout(this); row.setGravity(16);
            TextView name=new TextView(this); name.setText(CONTROLS[i]); name.setTextSize(18); name.setTextColor(-1); row.addView(name,new LinearLayout.LayoutParams(0,64,1));
            Spinner spinner=new Spinner(this); ArrayAdapter<String> adapter=new ArrayAdapter<>(this,android.R.layout.simple_spinner_dropdown_item,LABELS); spinner.setAdapter(adapter);
            int selected=DEFAULTS[i]; String action=values.getProperty(KEYS[i],ACTIONS[selected]); for(int a=0;a<ACTIONS.length;a++)if(ACTIONS[a].equals(action))selected=a;
            spinner.setSelection(selected); choices[i]=spinner; row.addView(spinner,new LinearLayout.LayoutParams(360,64)); content.addView(row);
        }
        swap=new Switch(this); swap.setText("Swap movement and turning sticks"); swap.setTextColor(-1); swap.setChecked("true".equals(values.getProperty("swap_sticks"))); content.addView(swap);
        TextView dz=new TextView(this); dz.setTextColor(-1); content.addView(dz);
        deadzone=new SeekBar(this); deadzone.setMax(45); int amount=15; try{amount=Math.round(Float.parseFloat(values.getProperty("deadzone","0.15"))*100);}catch(NumberFormatException ignored){} deadzone.setProgress(Math.max(0,Math.min(45,amount-5))); content.addView(deadzone);
        dz.setText("Stick deadzone: "+(deadzone.getProgress()+5)+"%");
        deadzone.setOnSeekBarChangeListener(new SeekBar.OnSeekBarChangeListener(){public void onProgressChanged(SeekBar s,int p,boolean user){dz.setText("Stick deadzone: "+(p+5)+"%");}public void onStartTrackingTouch(SeekBar s){}public void onStopTrackingTouch(SeekBar s){}});
        LinearLayout buttons=new LinearLayout(this); content.addView(buttons);
        Button reset=new Button(this); reset.setText("Reset defaults"); reset.setOnClickListener(v->{for(int i=0;i<choices.length;i++)choices[i].setSelection(DEFAULTS[i]);deadzone.setProgress(10);swap.setChecked(false);smoothing.setSelection(2);status.setText("Defaults restored. Press Save to apply.");}); buttons.addView(reset);
        Button save=new Button(this); save.setText("Save settings"); save.setOnClickListener(v->save()); buttons.addView(save);
        Button back=new Button(this); back.setText("Back"); back.setOnClickListener(v->finish()); buttons.addView(back);
        status=new TextView(this); status.setTextColor(0xffb9c8d7); status.setPadding(0,12,0,0); content.addView(status);
    }
    private void save(){
        File temp=new File(getFilesDir(),"quest_bindings.tmp");
        try(FileOutputStream out=new FileOutputStream(temp)){
            StringBuilder text=new StringBuilder("version=1\n");
            text.append("render_mode=").append(new String[]{"stereo","mono","panel"}[rendering.getSelectedItemPosition()]).append('\n');
            text.append("resolution=").append(QuestBridge.RESOLUTIONS[resolution.getSelectedItemPosition()]).append('\n');
            text.append("aim_smoothing=").append(new String[]{"off","light","balanced","strong"}[smoothing.getSelectedItemPosition()]).append('\n');
            text.append("weapon_tracking=").append(weaponTracking.getSelectedItemPosition()==0?"physical":"rotation").append('\n');
            for(int i=0;i<KEYS.length;i++)text.append(KEYS[i]).append('=').append(ACTIONS[choices[i].getSelectedItemPosition()]).append('\n');
            text.append("swap_sticks=").append(swap.isChecked()).append('\n').append("deadzone=").append((deadzone.getProgress()+5)/100.0f).append('\n');
            out.write(text.toString().getBytes(java.nio.charset.StandardCharsets.UTF_8)); out.getFD().sync();
        }catch(IOException e){status.setText("Could not save: "+e.getMessage());return;}
        if(!temp.renameTo(config())){status.setText("Could not replace saved bindings.");return;}
        QuestBridge.applyResolution(this);
        status.setText("Settings saved. Restart the game to apply them.");
    }
}
