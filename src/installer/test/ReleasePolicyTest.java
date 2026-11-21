import com.bo2vr.installer.ReleasePolicy;
public class ReleasePolicyTest {
    static void check(boolean value){if(!value)throw new AssertionError();}
    public static void main(String[] args)throws Exception{
        check(ReleasePolicy.newer(100,27));check(!ReleasePolicy.newer(100,100));check(!ReleasePolicy.newer(99,100));check(!ReleasePolicy.newer(-1,0));
        check(ReleasePolicy.allowedDownload(ReleasePolicy.FEED));
        check(ReleasePolicy.allowedDownload("https://release-assets.githubusercontent.com/abc?token=x"));
        for(String url:new String[]{"http://github.com/DurusIudex/BO2-Quest-VR/releases/a","https://github.com.evil.test/a","https://github.com/other/repo/releases/a","https://user@github.com/DurusIudex/BO2-Quest-VR/releases/a","https://github.com:444/DurusIudex/BO2-Quest-VR/releases/a","file:///sdcard/a.apk"})check(!ReleasePolicy.allowedDownload(url));
        check(ReleasePolicy.hash("abc".getBytes("UTF-8")).equals("ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"));
        check(!ReleasePolicy.validHash("abc"));
        ReleasePolicy.requireArtifact(ReleasePolicy.GAME,100,123,ReleasePolicy.CERT,ReleasePolicy.FEED);
        try{ReleasePolicy.requireArtifact("evil.package",100,123,ReleasePolicy.CERT,ReleasePolicy.FEED);throw new AssertionError();}catch(IllegalArgumentException expected){}
        try{ReleasePolicy.requireArtifact(ReleasePolicy.GAME,100,ReleasePolicy.MAX_APK+1,ReleasePolicy.CERT,ReleasePolicy.FEED);throw new AssertionError();}catch(IllegalArgumentException expected){}
        System.out.println("PASS: update versions, host restrictions, checksum and artifact bounds");
    }
}
