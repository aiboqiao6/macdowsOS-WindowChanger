# Preserve the complete host log while waiting for its successful Session::Apply
# and selected adapter, rather than racing the earlier target lease property.
Add-Type @'
using System.IO;
using System.Text;
using System.Threading;
using System.Threading.Tasks;
public sealed class NativeIslandHostOutput {
 private int ready;
 public bool IsReady {get{return Volatile.Read(ref ready)!=0;}}
 public Task<string> Completion {get;private set;}
 public NativeIslandHostOutput(StreamReader source) {Completion=Capture(source);}
 private async Task<string> Capture(StreamReader source) {
  var output=new StringBuilder();bool attached=false;string line;
  while((line=await source.ReadLineAsync().ConfigureAwait(false))!=null) {
   output.AppendLine(line);
   if(line=="attached=1")attached=true;
   if(attached&&line=="adapter=288")Volatile.Write(ref ready,1);
  }
  return output.ToString();
 }
}
'@
