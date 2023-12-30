{# ------------------------------------------------------------------ #}
{# Copyright (c) 2022 Firebuild Inc.                                  #}
{# All rights reserved.                                               #}
{# Free for personal use and commercial trial.                        #}
{# Non-trial commercial use requires licenses available from          #}
{# https://firebuild.com.                                             #}
{# Modification and redistribution are permitted, but commercial use  #}
{# of derivative works is subject to the same requirements of this    #}
{# license.                                                           #}
{# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,    #}
{# EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF #}
{# MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND              #}
{# NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT        #}
{# HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,       #}
{# WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, #}
{# OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER      #}
{# DEALINGS IN THE SOFTWARE.                                          #}
{# ------------------------------------------------------------------ #}
{# Template for the dlopen() family.                                  #}
{# ------------------------------------------------------------------ #}
### extends "tpl.c"

{% set msg_add_fields = ["if (absolute_filename == NULL && filename && strrchr(filename, '/')) {",
                         "  /* This is a relative or absolute name which will be made absolute in the next step. */",
                         "  absolute_filename = filename;",
                         "}",
                         "if (absolute_filename != NULL) BUILDER_SET_ABSOLUTE_CANONICAL(" + msg + ", absolute_filename);",
                         "fbbcomm_builder_dlopen_set_error(&ic_msg, !success);"] %}

### block before
  /* TODO(rbalint) Save all loaded images before the dlopen() to collect also loaded shared
   * library dependencies. */
  FB_THREAD_LOCAL(interception_recursion_depth)++;
### endblock before

### block after
  FB_THREAD_LOCAL(interception_recursion_depth)--;

  const char *absolute_filename = NULL;
  if (success) {
###   if target == "darwin"
    /* From https://github.com/JuliaLang/julia/blob/0027ed143e90d0f965694de7ea8c692d75ffa1a5/src/sys.c#L572-L583 .
     * MIT licensed originally.
     */
    /* Iterate through all images currently in memory */
    int32_t i;
    for (i = _dyld_image_count() - 1; i > 1 ; i--) {
      /* dlopen() each image, check handle */
      const char *image_name = _dyld_get_image_name(i);
      void *probe_handle = get_ic_orig_dlopen()(image_name, RTLD_LAZY | RTLD_NOLOAD);
      /* If the handle is the same as what was passed in (modulo mode bits), return this image name */
      dlclose(probe_handle);
      if (((intptr_t)ret & (-4)) == ((intptr_t)probe_handle & (-4))) {
        absolute_filename = image_name;
        break;
      }
    }
###   else
    struct link_map *map;
    if (dlinfo(ret, RTLD_DI_LINKMAP, &map) == 0) {
      /* Note: contrary to the dlinfo(3) manual page, this is not necessarily absolute. See #657.
       * We'll resolve to absolute when setting the FBB field. */
      absolute_filename = map->l_name;
    } else {
      /* As per #920, dlinfo() returning an error _might_ cause problems later on in the intercepted
       * app, should it call dlerror(). A call to dlerror() would return a non-NULL string
       * describing dlinfo()'s failure, rather than NULL describing dlopen()'s success. But why
       * would any app invoke dlerror() after a successful dlopen()? Let's hope that in practice no
       * application does this. */
    }
###   endif
  }
### endblock after
