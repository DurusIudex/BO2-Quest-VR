package com.bo2vr.installer;

import java.io.*;
import java.net.*;
import java.nio.file.*;
import java.security.*;
import java.security.cert.*;
import java.util.*;
import java.util.regex.*;
import javax.crypto.Mac;
import javax.crypto.spec.SecretKeySpec;
import javax.net.ssl.*;

/** Platform-independent transfer logic, also exercised against the PC helper. */
public final class TransferClient {
    public interface Progress {void update(String file,long current,long total)throws IOException;}
    public static final class Entry {
        public final String path,sha;public final long size;
        public Entry(String path,long size,String sha)throws IOException{
            if(!safePath(path)||size<0||size>16L*1024*1024*1024||!ReleasePolicy.validHash(sha))throw new IOException("Invalid game-file entry");
            this.path=path;this.size=size;this.sha=sha;
        }
    }
    public static boolean safePath(String p){
        if(p==null||p.length()>240||!p.matches("[A-Za-z0-9_./-]+"))return false;
        for(String part:p.split("/",-1))if(part.isEmpty()||part.equals(".")||part.equals(".."))return false;
        if(p.equals("localization.txt")||p.equals("localization_zm.txt")||p.equals("localization_mp.txt"))return true;
        return p.matches("(zone|main|sound|video)/.+\\.(ff|ipak|xpak|iwd|sabs|sabl|bik|webm|clump)");
    }
    public static String normalizeCode(String code)throws IOException{
        code=code.toUpperCase(Locale.ROOT).replace("-","").replace(" ","");
        if(!code.matches("[A-HJ-NP-Z2-9]{12}"))throw new IOException("Enter the 12-character code shown on your PC");return code;
    }
    private static boolean local(InetAddress ip){return ip instanceof Inet4Address&&(ip.isSiteLocalAddress()||ip.isLoopbackAddress());}
    public static String proof(String code,String nonce,String certificate,int port)throws Exception{
        Mac mac=Mac.getInstance("HmacSHA256");mac.init(new SecretKeySpec(code.getBytes("UTF-8"),"HmacSHA256"));
        return ReleasePolicy.hex(mac.doFinal(("BO2TRANSFER1\n"+nonce+"\n"+certificate+"\n"+port).getBytes("UTF-8")));
    }
    private static String field(String json,String key)throws IOException{
        Matcher m=Pattern.compile("\""+key+"\"\\s*:\\s*\"([^\"]*)\"").matcher(json);
        if(!m.find())throw new IOException("Invalid pairing reply");return m.group(1);
    }
    public static TransferClient pair(String host,String rawCode)throws Exception{
        String code=normalizeCode(rawCode);byte[] random=new byte[16];new SecureRandom().nextBytes(random);String nonce=ReleasePolicy.hex(random);
        byte[] request=("{\"service\":\"BO2TRANSFER1\",\"nonce\":\""+nonce+"\"}").getBytes("UTF-8");
        Set<InetAddress> destinations=new HashSet<>();
        if(!host.trim().isEmpty()){
            if(!host.matches("[0-9.]{7,15}"))throw new IOException("Use the PC's local IPv4 address");
            InetAddress ip=InetAddress.getByName(host);if(!local(ip))throw new IOException("PC transfer is available only on your local network");destinations.add(ip);
        }else{
            destinations.add(InetAddress.getByName("255.255.255.255"));
            Enumeration<NetworkInterface> all=NetworkInterface.getNetworkInterfaces();while(all.hasMoreElements())for(InterfaceAddress a:all.nextElement().getInterfaceAddresses())if(a.getBroadcast()!=null)destinations.add(a.getBroadcast());
        }
        try(DatagramSocket socket=new DatagramSocket()){
            socket.setBroadcast(true);socket.setSoTimeout(1000);
            for(int attempt=0;attempt<3;attempt++){
                for(InetAddress ip:destinations)socket.send(new DatagramPacket(request,request.length,ip,27852));
                long end=System.currentTimeMillis()+3000;
                while(System.currentTimeMillis()<end){
                    byte[] response=new byte[2048];DatagramPacket packet=new DatagramPacket(response,response.length);
                    try{socket.receive(packet);}catch(SocketTimeoutException e){continue;}
                    if(!local(packet.getAddress()))continue;
                    try{
                        String json=new String(response,0,packet.getLength(),"UTF-8");
                        if(!field(json,"service").equals("BO2TRANSFER1")||!field(json,"nonce").equals(nonce))continue;
                        String cert=field(json,"certificate"),signature=field(json,"proof");
                        Matcher p=Pattern.compile("\"port\"\\s*:\\s*(\\d{1,5})").matcher(json);if(!p.find())continue;int port=Integer.parseInt(p.group(1));
                        if(!ReleasePolicy.validHash(cert)||port<1024||port>65535)continue;
                        if(!MessageDigest.isEqual(proof(code,nonce,cert,port).getBytes("UTF-8"),signature.getBytes("UTF-8")))continue;
                        return new TransferClient(packet.getAddress().getHostAddress(),port,cert,code);
                    }catch(IOException|IllegalArgumentException ignored){}
                }
            }
        }throw new IOException("PC not found or code incorrect. Keep the helper open, use the same Wi-Fi, and allow its Private network firewall prompt. You can enter the PC address manually.");
    }
    private final String base,code,fingerprint;private final SSLSocketFactory sockets;
    public TransferClient(String address,int port,String fingerprint,String code)throws Exception{
        if(!local(InetAddress.getByName(address))||!ReleasePolicy.validHash(fingerprint))throw new IOException("Invalid paired endpoint");
        this.base="https://"+address+":"+port;this.code=normalizeCode(code);this.fingerprint=fingerprint;
        X509TrustManager pin=new X509TrustManager(){
            public X509Certificate[] getAcceptedIssuers(){return new X509Certificate[0];}
            public void checkClientTrusted(X509Certificate[] chain,String auth)throws CertificateException{throw new CertificateException("Client certificate not supported");}
            public void checkServerTrusted(X509Certificate[] chain,String auth)throws CertificateException{
                try{if(chain==null||chain.length==0||!ReleasePolicy.hash(chain[0].getEncoded()).equals(fingerprint))throw new CertificateException("PC certificate changed; pair again");chain[0].checkValidity();}
                catch(CertificateException e){throw e;}catch(Exception e){throw new CertificateException(e);}
            }
        };
        SSLContext tls=SSLContext.getInstance("TLS");tls.init(null,new TrustManager[]{pin},new SecureRandom());sockets=tls.getSocketFactory();
    }
    private HttpsURLConnection open(String path,long offset)throws Exception{
        HttpsURLConnection c=(HttpsURLConnection)new URL(base+path).openConnection();c.setSSLSocketFactory(sockets);
        c.setHostnameVerifier((host,session)->{try{return ReleasePolicy.hash(session.getPeerCertificates()[0].getEncoded()).equals(fingerprint);}catch(Exception e){return false;}});
        c.setInstanceFollowRedirects(false);c.setConnectTimeout(15000);c.setReadTimeout(30000);c.setRequestProperty("Authorization","Bearer "+code);
        if(offset>0)c.setRequestProperty("Range","bytes="+offset+"-");return c;
    }
    public byte[] manifest()throws Exception{
        HttpsURLConnection c=open("/manifest",0);try{
            if(c.getResponseCode()!=200)throw new IOException("PC rejected pairing or transfer expired");
            ByteArrayOutputStream out=new ByteArrayOutputStream();try(InputStream in=c.getInputStream()){byte[] b=new byte[8192];int n;while((n=in.read(b))!=-1){if(out.size()+n>2*1024*1024)throw new IOException("Manifest too large");out.write(b,0,n);}}return out.toByteArray();
        }finally{c.disconnect();}
    }
    public static String hash(File f)throws Exception{
        MessageDigest d=MessageDigest.getInstance("SHA-256");try(InputStream in=new FileInputStream(f)){byte[] b=new byte[256*1024];int n;while((n=in.read(b))!=-1)d.update(b,0,n);}return ReleasePolicy.hex(d.digest());
    }
    public static File target(File root,String relative)throws IOException{
        File p=new File(root,relative).getCanonicalFile();String prefix=root.getCanonicalPath()+File.separator;
        if(!p.getPath().startsWith(prefix))throw new IOException("File path escapes BO2 folder");return p;
    }
    public void install(File root,List<Entry> entries,Progress progress)throws Exception{
        long total=0,done=0;Set<String> names=new HashSet<>();
        for(Entry e:entries){if(!names.add(e.path.toLowerCase(Locale.ROOT)))throw new IOException("Duplicate game-file path");total=Math.addExact(total,e.size);}
        if(total>100L*1024*1024*1024||entries.size()>10000)throw new IOException("Game manifest exceeds limits");
        for(Entry e:entries){
            progress.update("Checking "+e.path,done,total);File destination=target(root,e.path);
            if(destination.isFile()&&destination.length()==e.size&&hash(destination).equals(e.sha)){done+=e.size;progress.update(e.path,done,total);continue;}
            File part=target(root,".transfer/"+e.path+".part");if(!part.getParentFile().isDirectory()&&!part.getParentFile().mkdirs())throw new IOException("Cannot create BO2 folder; allow file access");
            if(part.length()>e.size&&!part.delete())throw new IOException("Cannot reset partial transfer");long offset=part.length();
            if(part.getUsableSpace()<e.size-offset+64L*1024*1024)throw new IOException("Not enough Quest storage for "+e.path);
            if(offset<e.size){
                HttpsURLConnection c=open("/files/"+e.path,offset);try{
                    int response=c.getResponseCode();if(response!=(offset>0?206:200))throw new IOException("PC transfer failed ("+response+")");
                    if(offset>0&&!Objects.equals(c.getHeaderField("Content-Range"),"bytes "+offset+"-"+(e.size-1)+"/"+e.size))throw new IOException("Invalid resume response");
                    try(InputStream in=c.getInputStream();FileOutputStream out=new FileOutputStream(part,offset>0)){
                        byte[] b=new byte[256*1024];int n;long copied=offset,last=0;
                        while((n=in.read(b))!=-1){copied+=n;if(copied>e.size)throw new IOException("Game file exceeds expected size");out.write(b,0,n);long now=System.currentTimeMillis();if(now-last>200){progress.update(e.path,done+copied,total);last=now;}}
                        out.getFD().sync();if(copied!=e.size)throw new IOException("Transfer interrupted; connect again to resume");
                    }
                }finally{c.disconnect();}
            }else if(!part.exists())new FileOutputStream(part).close();
            if(!hash(part).equals(e.sha)){part.delete();throw new IOException("Game-file checksum mismatch; retry the transfer");}
            if(!destination.getParentFile().isDirectory()&&!destination.getParentFile().mkdirs())throw new IOException("Cannot create game directory");
            try{Files.move(part.toPath(),destination.toPath(),StandardCopyOption.ATOMIC_MOVE,StandardCopyOption.REPLACE_EXISTING);}
            catch(AtomicMoveNotSupportedException e1){Files.move(part.toPath(),destination.toPath(),StandardCopyOption.REPLACE_EXISTING);}
            done+=e.size;progress.update(e.path,done,total);
        }
    }
}
