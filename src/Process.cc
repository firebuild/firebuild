
#include "Process.h"

using namespace std;
namespace firebuild {
  
static int fb_pid_counter;

Process::Process (int pid, int ppid, process_type type) : type(type), pid(pid), ppid(ppid)
{
  fb_pid = fb_pid_counter++;
  state = FB_PROC_RUNNING;
}

void Process::exit_result (int status, long int utime_m, long int stime_m)
{
  state = FB_PROC_FINISHED;
  exit_status = status;
  this->utime_m = utime_m;
  this->stime_m = stime_m;
}
}

