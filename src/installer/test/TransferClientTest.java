import com.bo2vr.installer.*;
import java.io.*;
import java.nio.file.*;
import java.util.*;

public class TransferClientTest {
    static void check(boolean ok){if(!ok)throw new AssertionError();}
    public static void main(String[] args)throws Exception{
        for(String bad:new String[]{"../players/config.cfg","zone/../../secret.ff","/zone/all/test.ff","zone\\all\\test.ff","zone//all/test.ff","players/config.cfg","zone/all/tool.exe","zone/all/test.ff/..","C:/zone/all/test.ff"})check(!TransferClient.safePath(bad));
        check(TransferClient.safePath("zone/all/zm_transit.ff"));check(TransferClient.safePath("localization_mp.txt"));
        check(TransferClient.normalizeCode("abcd-efgh-jklm").equals("ABCDEFGHJKLM"));
        File root=new File(args[0]);root.mkdirs();File data=new File(args[1]);String sha=TransferClient.hash(data);
        String fingerprint=args[2],code=args[3];int port=Integer.parseInt(args[4]);
        TransferClient paired=TransferClient.pair("127.0.0.1",code);check(new String(paired.manifest(),"UTF-8").contains("zm_transit.ff"));
        TransferClient client=new TransferClient("127.0.0.1",port,fingerprint,code);
        TransferClient.Entry entry=new TransferClient.Entry("zone/all/zm_transit.ff",data.length(),sha);
        File partial=TransferClient.target(root,".transfer/zone/all/zm_transit.ff.part");partial.getParentFile().mkdirs();
        byte[] bytes=Files.readAllBytes(data.toPath());Files.write(partial.toPath(),Arrays.copyOf(bytes,137));
        client.install(root,Arrays.asList(entry),(f,n,t)->{});
        File result=TransferClient.target(root,entry.path);check(TransferClient.hash(result).equals(sha));check(!partial.exists());
        long modified=result.lastModified();client.install(root,Arrays.asList(entry),(f,n,t)->{});check(result.lastModified()==modified);
        try{new TransferClient("127.0.0.1",port,String.join("",Collections.nCopies(64,"0")),code).manifest();throw new AssertionError("Wrong pin accepted");}catch(javax.net.ssl.SSLException expected){}
        try{new TransferClient("127.0.0.1",port,fingerprint,"ABCDEFGHJKLN").manifest();throw new AssertionError("Wrong code accepted");}catch(IOException expected){}
        try{client.install(root,Arrays.asList(entry,entry),(f,n,t)->{});throw new AssertionError("Duplicate accepted");}catch(IOException expected){}
        TransferClient.Entry corrupted=new TransferClient.Entry(entry.path,entry.size,String.join("",Collections.nCopies(64,"1")));
        try{client.install(root,Arrays.asList(corrupted),(f,n,t)->{});throw new AssertionError("Bad hash accepted");}catch(IOException expected){}
        check(TransferClient.hash(result).equals(sha));
        try{TransferClient.target(root,"../escape.ff");throw new AssertionError("Traversal accepted");}catch(IOException expected){}
        System.out.println("PASS: authenticated pairing, TLS pin rejection, bad code rejection, resume, hash verification, skip, path bounds and preservation on failure");
    }
}
