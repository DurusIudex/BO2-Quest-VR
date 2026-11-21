package com.bo2vr.installer;

import java.net.URI;
import java.security.MessageDigest;

public final class ReleasePolicy {
    public static final String REPO="DurusIudex/BO2-Quest-VR";
    public static final String FEED="https://github.com/"+REPO+"/releases/latest/download/update.json";
    public static final String CERT="0b8ea00d9dc8a9528aa5ed4c1e0d769b2c875c8c87b8050eb9e71e43b325f51a";
    public static final String GAME="com.bo2vr.questdev", LAUNCHER="com.bo2vr.installer";
    public static final long MAX_APK=256L*1024*1024;
    public static boolean newer(long remote,long installed){return remote>0&&remote>installed;}
    public static boolean allowedDownload(String url){
        try{
            URI u=new URI(url);
            if(!"https".equals(u.getScheme())||u.getUserInfo()!=null||(u.getPort()!=-1&&u.getPort()!=443))return false;
            if("github.com".equals(u.getHost()))return u.getPath().startsWith("/"+REPO+"/releases/");
            return "release-assets.githubusercontent.com".equals(u.getHost());
        }catch(Exception e){return false;}
    }
    public static boolean validHash(String s){return s!=null&&s.matches("[0-9a-f]{64}");}
    public static String hex(byte[] bytes){StringBuilder s=new StringBuilder();for(byte b:bytes)s.append(String.format(java.util.Locale.ROOT,"%02x",b&255));return s.toString();}
    public static String hash(byte[] bytes)throws Exception{return hex(MessageDigest.getInstance("SHA-256").digest(bytes));}
    public static void requireArtifact(String pkg,long version,long size,String hash,String url){
        if(!(GAME.equals(pkg)||LAUNCHER.equals(pkg))||version<1||size<1||size>MAX_APK||!validHash(hash)||!allowedDownload(url))
            throw new IllegalArgumentException("Invalid release metadata");
    }
}
