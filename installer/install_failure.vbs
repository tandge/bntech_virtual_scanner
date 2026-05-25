If Session.Property("APP_LANGUAGE") = "zh_CN" Then
  MsgBox "BN Tech 虚拟扫描仪安装失败。" & vbCrLf & vbCrLf & _
         "可能原因：" & vbCrLf & _
         "- TWAIN 数据源文件正在被扫描应用占用。" & vbCrLf & _
         "- 安装程序没有足够权限。" & vbCrLf & _
         "- 无法写入 Windows TWAIN 目录。" & vbCrLf & _
         "- 源文件缺失或被锁定。" & vbCrLf & vbCrLf & _
         "请关闭扫描应用，以管理员身份运行安装程序后重试。", _
         vbExclamation, "BN Tech 虚拟扫描仪安装"
Else
  MsgBox "BN Tech Virtual Scanner installation failed." & vbCrLf & vbCrLf & _
         "Possible causes:" & vbCrLf & _
         "- The TWAIN data source file is in use by a scanning application." & vbCrLf & _
         "- The installer was not run with sufficient permissions." & vbCrLf & _
         "- Files under the Windows TWAIN folder could not be written." & vbCrLf & _
         "- Required source files were missing or locked." & vbCrLf & vbCrLf & _
         "Please close scanning applications, run the installer as administrator, and try again.", _
         vbExclamation, "BN Tech Virtual Scanner Setup"
End If
