If Session.Property("APP_LANGUAGE") = "zh_CN" Then
  MsgBox "BN Tech 虚拟扫描仪卸载成功。" & vbCrLf & vbCrLf & _
         "TWAIN 数据源已从 Windows TWAIN 目录移除。", _
         vbInformation, "BN Tech 虚拟扫描仪安装"
Else
  MsgBox "BN Tech Virtual Scanner uninstalled successfully." & vbCrLf & vbCrLf & _
         "The TWAIN data source has been removed from the Windows TWAIN folder.", _
         vbInformation, "BN Tech Virtual Scanner Setup"
End If
