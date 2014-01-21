#ifndef FIREBUILD_DEBUG_H
#define FIREBUILD_DEBUG_H

#include <string>

namespace firebuild {

/** Send error message to supervisor or print error message in supervisor */
void fb_error(const std::string &msg);

/**
 * Send debug message to supervisor or printe debug message in supervisor
 * if debug level is at least lvl
 */
#define FB_DEBUG(lvl, msg) if (lvl <= firebuild::debug_level) firebuild::fb_debug(msg)

void fb_debug(const std::string &msg);

/** current debugging level */
extern int debug_level;

}
#endif
