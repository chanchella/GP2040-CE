#!/usr/bin/env python3
"""Publish the two verified, immutable firmware download artifacts."""
import hashlib,json,os,urllib.error,urllib.parse,urllib.request
from pathlib import Path
repo=os.environ['GITHUB_REPOSITORY'];commit=os.environ['GITHUB_SHA'];token=os.environ['GITHUB_TOKEN']
assert repo=='chanchella/GP2040-CE'
tag='oag-mobile-touch-games-v2-bt-km-fix1-817948d6'
expected={
 'OAG_MOBILE_TOUCH_GAMES_V2_BT_KM_FIX1_Pico2W.uf2':('817948d651e3e90c917ab0449eb6054d0c2283a2453095d435a177dd1d602571',1718272),
 'OAG_MOBILE_TOUCH_GAMES_V2_BT_KM_FIX1_FULL_SOURCE_AND_UF2.zip':('174a1f31884790d532e6a03f99c428e41797fde878bf8faf9149c7f210c2a87e',95261052)}
base='https://api.github.com/repos/'+repo
headers={'Authorization':'Bearer '+token,'Accept':'application/vnd.github+json',
 'X-GitHub-Api-Version':'2022-11-28','User-Agent':'OAG-verified-firmware-delivery'}
def request(method,url,data=None,content_type='application/json'):
 h={**headers,'Content-Type':content_type}
 req=urllib.request.Request(url,data=data,headers=h,method=method)
 with urllib.request.urlopen(req,timeout=240) as response:return json.load(response)
for filename,(digest,size) in expected.items():
 data=Path('downloads',filename).read_bytes();assert len(data)==size and hashlib.sha256(data).hexdigest()==digest,filename
try:release=request('GET',base+'/releases/tags/'+tag)
except urllib.error.HTTPError as error:
 if error.code!=404:raise
 body={'tag_name':tag,'target_commitish':commit,
  'name':'OAG Mobile Touch Games V2 — Android Bluetooth / KM FIX1 — Pico 2 W',
  'body':Path('downloads/RELEASE_NOTES_AR.md').read_text(),'draft':False,'prerelease':True}
 release=request('POST',base+'/releases',json.dumps(body,ensure_ascii=False).encode('utf8'))
upload=release['upload_url'].split('{')[0]
assert upload.startswith('https://uploads.github.com/repos/'+repo+'/releases/')
assets={a['name']:a for a in release.get('assets',[])}
for filename,(digest,size) in expected.items():
 if filename in assets:
  assert assets[filename]['size']==size
  assert assets[filename].get('digest') in {None,'sha256:'+digest}
  continue
 asset=request('POST',upload+'?'+urllib.parse.urlencode({'name':filename}),
  Path('downloads',filename).read_bytes(),'application/zip' if filename.endswith('.zip') else 'application/octet-stream')
 assert asset['name']==filename and asset['size']==size
 assert asset.get('digest') in {None,'sha256:'+digest}
 assets[filename]=asset
 print('VERIFIED_RELEASE_ASSET',filename,size,asset.get('digest'),asset['browser_download_url'],flush=True)
print('RELEASE_URL',release['html_url'],flush=True)
