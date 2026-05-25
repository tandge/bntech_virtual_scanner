Option Explicit

Dim shell, fso, tempDir, resultFile, htaFile, hta, exec, result
Set shell = CreateObject("WScript.Shell")
Set fso = CreateObject("Scripting.FileSystemObject")
tempDir = shell.ExpandEnvironmentStrings("%TEMP%")
resultFile = tempDir & "\bntech_installer_language.txt"
htaFile = tempDir & "\bntech_installer_language.hta"

On Error Resume Next
If fso.FileExists(resultFile) Then fso.DeleteFile resultFile, True
If fso.FileExists(htaFile) Then fso.DeleteFile htaFile, True
On Error GoTo 0

hta = "<html><head><title>BN Tech Virtual Scanner Setup</title>" & vbCrLf & _
"<HTA:APPLICATION ID='app' APPLICATIONNAME='BN Tech Virtual Scanner Setup' BORDER='dialog' CAPTION='yes' SHOWINTASKBAR='yes' SINGLEINSTANCE='yes' SYSMENU='yes' SCROLL='no' />" & vbCrLf & _
"<meta http-equiv='X-UA-Compatible' content='IE=11' />" & vbCrLf & _
"<style>body{font-family:Segoe UI,Arial,sans-serif;margin:22px;width:360px;height:170px;}h2{font-size:18px;margin:0 0 14px 0;}label{display:block;margin-bottom:6px;}select{width:260px;padding:4px;}button{min-width:82px;padding:6px 14px;margin-left:8px;}.buttons{text-align:right;margin-top:28px;}</style>" & vbCrLf & _
"<script language='javascript'>" & vbCrLf & _
"var resultPath='" & Replace(resultFile, "\", "\\") & "';" & vbCrLf & _
"function fso(){return new ActiveXObject('Scripting.FileSystemObject');}" & vbCrLf & _
"function writeResult(v){var t=fso().CreateTextFile(resultPath,true,false);t.Write(v);t.Close();}" & vbCrLf & _
"function refresh(){var zh=document.getElementById('lang').value=='zh_CN';document.title=zh?'BN Tech 虚拟扫描仪安装':'BN Tech Virtual Scanner Setup';document.getElementById('title').innerText=zh?'选择安装语言':'Choose setup language';document.getElementById('desc').innerText=zh?'请选择扫描仪设置界面使用的语言。':'Select the language used by the scanner settings UI.';document.getElementById('label').innerText=zh?'语言：':'Language:';document.getElementById('install').innerText=zh?'安装':'Install';document.getElementById('cancel').innerText=zh?'取消':'Cancel';}" & vbCrLf & _
"function install(){writeResult(document.getElementById('lang').value);window.close();}" & vbCrLf & _
"function cancel(){writeResult('CANCEL');window.close();}" & vbCrLf & _
"window.onload=function(){resizeTo(430,270);refresh();};" & vbCrLf & _
"window.onbeforeunload=function(){try{if(!fso().FileExists(resultPath))writeResult('CANCEL');}catch(e){}};" & vbCrLf & _
"</script></head><body>" & vbCrLf & _
"<h2 id='title'>Choose setup language</h2>" & vbCrLf & _
"<div id='desc'>Select the language used by the scanner settings UI.</div><br/>" & vbCrLf & _
"<label id='label' for='lang'>Language:</label>" & vbCrLf & _
"<select id='lang' onchange='refresh()'><option value='en_US' selected>English</option><option value='zh_CN'>简体中文</option></select>" & vbCrLf & _
"<div class='buttons'><button id='cancel' onclick='cancel()'>Cancel</button><button id='install' onclick='install()'>Install</button></div>" & vbCrLf & _
"</body></html>"

Dim file
Set file = fso.CreateTextFile(htaFile, True, True)
file.Write hta
file.Close

Set exec = shell.Exec("mshta.exe """ & htaFile & """")
Do While exec.Status = 0
  WScript.Sleep 100
Loop

If fso.FileExists(resultFile) Then
  result = Trim(fso.OpenTextFile(resultFile, 1, False).ReadAll)
Else
  result = "CANCEL"
End If

On Error Resume Next
If fso.FileExists(resultFile) Then fso.DeleteFile resultFile, True
If fso.FileExists(htaFile) Then fso.DeleteFile htaFile, True
On Error GoTo 0

If UCase(result) = "CANCEL" Or result = "" Then
  Err.Raise 1602, "BN Tech Virtual Scanner Setup", "Installation canceled by user."
End If

Session.Property("APP_LANGUAGE") = result
