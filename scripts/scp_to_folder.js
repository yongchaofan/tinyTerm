var xml = new ActiveXObject("Microsoft.XMLHTTP");
var port = "8080/?";
if ( WScript.Arguments.length>0 ) port = WScript.Arguments(0)+"/?";
var filename = term("!Selection");
var objShell = new ActiveXObject("Shell.Application");
var objFolder = objShell.BrowseForFolder(0,"Destination folder",0x11,"");
if ( objFolder ) term("!scp :"+filename+" "+objFolder.Self.path);
function term( cmd ) {
   xml.Open ("GET", "http://127.0.0.1:"+port+cmd, false);
   xml.Send();
   return xml.responseText;
}