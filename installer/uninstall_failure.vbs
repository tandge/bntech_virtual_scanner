If Session.Property("APP_LANGUAGE") = "zh_CN" Then
  MsgBox "BN Tech 虚拟扫描仪卸载失败。" & vbCrLf & vbCrLf & _
         "可能原因：" & vbCrLf & _
         "- TWAIN 数据源文件正在被扫描应用占用。" & vbCrLf & _
         "- 卸载程序没有足够权限。" & vbCrLf & _
         "- 无法从 Windows TWAIN 目录移除文件。" & vbCrLf & _
         "- 安全软件或系统策略阻止卸载。" & vbCrLf & vbCrLf & _
         "请关闭扫描应用，以管理员身份运行卸载程序后重试。", _
         vbExclamation, "BN Tech 虚拟扫描仪安装"
Else
  MsgBox "BN Tech Virtual Scanner uninstall failed." & vbCrLf & vbCrLf & _
         "Possible causes:" & vbCrLf & _
         "- The TWAIN data source file is in use by a scanning application." & vbCrLf & _
         "- The uninstaller was not run with sufficient permissions." & vbCrLf & _
         "- Files under the Windows TWAIN folder could not be removed." & vbCrLf & _
         "- Security software or system policy blocked the uninstall." & vbCrLf & vbCrLf & _
         "Please close scanning applications, run the uninstaller as administrator, and try again.", _
         vbExclamation, "BN Tech Virtual Scanner Setup"
End If
