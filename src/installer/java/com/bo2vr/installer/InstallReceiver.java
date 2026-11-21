package com.bo2vr.installer;

import android.content.*;
import android.content.pm.PackageInstaller;

public final class InstallReceiver extends BroadcastReceiver {
    @Override public void onReceive(Context context,Intent intent){
        int status=intent.getIntExtra(PackageInstaller.EXTRA_STATUS,PackageInstaller.STATUS_FAILURE);
        if(status==PackageInstaller.STATUS_PENDING_USER_ACTION){
            Intent confirm=intent.getParcelableExtra(Intent.EXTRA_INTENT);
            if(confirm!=null){
                context.getSharedPreferences("launcher",0).edit().putString("status","Confirm installation in the Quest system dialog.").apply();
                try{context.startActivity(confirm.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK));return;}
                catch(RuntimeException ignored){}
            }
        }
        String message=status==PackageInstaller.STATUS_SUCCESS?"Installation complete. Ready to play.":
            status==PackageInstaller.STATUS_FAILURE_ABORTED?"Installation cancelled. You can retry whenever you’re ready.":
            "Installation could not finish. Tap Check for updates to retry. (Code "+status+")";
        context.getSharedPreferences("launcher",0).edit().putString("status",message).remove("pending").remove("pendingArtifact").apply();
        context.startActivity(new Intent(context,MainActivity.class).addFlags(Intent.FLAG_ACTIVITY_NEW_TASK|Intent.FLAG_ACTIVITY_CLEAR_TOP));
    }
}
