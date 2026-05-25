If Session.Property("APP_LANGUAGE") = "zh_CN" Then
  MsgBox "BN Tech 虚拟扫描仪安装成功。" & vbCrLf & vbCrLf & _
         "TWAIN 数据源已安装到 Windows TWAIN 目录。", _
         vbInformation, "BN Tech 虚拟扫描仪安装"
Else
  MsgBox "BN Tech Virtual Scanner installed successfully." & vbCrLf & vbCrLf & _
         "The TWAIN data source has been installed to the Windows TWAIN folder.", _
         vbInformation, "BN Tech Virtual Scanner Setup"
End If
