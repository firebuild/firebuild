
#include "Process.h"

using namespace std;
namespace firebuild {
  
static int fb_pid_counter;

Process::Process (int pid, int ppid, process_type type) : type(type), pid(pid), ppid(ppid)
{
  fb_pid = fb_pid_counter++;
  state = FB_PROC_RUNNING;
}

void Process::update_rusage (long int utime_m, long int stime_m)
{
  this->utime_m = utime_m;
  this->stime_m = stime_m;
}

void Process::exit_result (int status, long int utime_m, long int stime_m)
{
  state = FB_PROC_FINISHED;
  exit_status = status;
  update_rusage(utime_m, stime_m);
}

void Process::sum_rusage(long int *sum_utime_m, long int *sum_stime_m)
{
  (*sum_utime_m) += utime_m;
  (*sum_stime_m) += stime_m;
  for (unsigned int i = 0; i < children.size(); i++) {
    children[i]->sum_rusage(sum_utime_m, sum_stime_m);
  }
}

}

