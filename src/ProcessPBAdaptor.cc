
#include "ProcessPBAdaptor.h"

using namespace std;
namespace firebuild {
int ProcessPBAdaptor::msg(Process &p, const msg::Open &o)
{
  bool c = (o.has_created())?o.created():false;
  int error = (o.has_error_no())?o.error_no():0;
  return p.open_file(o.file(), o.flags(), o.mode(), o.ret(), c, error);
}

}
