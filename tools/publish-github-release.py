"""Publish staged release assets with the existing Git Credential Manager login.

Credentials remain in the credential manager and memory, never in files or logs.
Run after committing and pushing the source. Draft stays unpublished on failure.
"""
import json,os,subprocess,urllib.request,urllib.parse
from pathlib import Path
root=Path(__file__).resolve().parents[1]
cfg=json.loads((root/'release.json').read_text())
credential=subprocess.run(['git','credential','fill'],input='protocol=https\nhost=github.com\n\n',capture_output=True,text=True,check=True,env={**os.environ,'GIT_TERMINAL_PROMPT':'0','GCM_INTERACTIVE':'never'})
auth=dict(line.split('=',1) for line in credential.stdout.splitlines() if '=' in line)['password']
def request(url,method='GET',data=None,content_type='application/json'):
    payload=json.dumps(data).encode() if isinstance(data,dict) else data
    req=urllib.request.Request(url,method=method,data=payload,headers={'Authorization':'Bearer '+auth,'Accept':'application/vnd.github+json','User-Agent':'BO2QuestVR-release','Content-Type':content_type})
    with urllib.request.urlopen(req,timeout=180) as response:return json.load(response)
repo=cfg['repository'];base='https://api.github.com/repos/'+repo
identity=request('https://api.github.com/user')
assert identity['login'].lower()==repo.split('/')[0].lower(),'Unexpected publishing account'
head=subprocess.check_output(['git','rev-parse','HEAD'],cwd=root,text=True).strip()
remote=request(base+'/commits/main')['sha'];assert head==remote,'Push the current source commit before publishing'
notes=(root/'RELEASE_NOTES.md').read_text()
release=request(base+'/releases','POST',{'tag_name':'v'+cfg['version'],'target_commitish':head,'name':'BO2 Quest VR '+cfg['version'],'body':notes,'draft':True,'prerelease':False})
print('Created draft release',release['id'])
for name in ['BO2-Quest-VR.apk','BO2-Quest-Installer.apk','update.json','SHA256SUMS.txt']:
    path=root/'build/release'/name
    asset=request(release['upload_url'].split('{')[0]+'?name='+urllib.parse.quote(name),'POST',path.read_bytes(),'application/octet-stream')
    assert asset['size']==path.stat().st_size
    print('Uploaded',name,asset['size'])
published=request(base+'/releases/'+str(release['id']),'PATCH',{'draft':False,'make_latest':'true'})
print(published['html_url'])
