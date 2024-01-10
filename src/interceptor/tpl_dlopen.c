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

{% set msg_add_fields = ["fbbcomm_builder_dlopen_set_libs_with_count(&ic_msg, (const char * const *)new_libs, new_libs_count);",
                         "fbbcomm_builder_dlopen_set_error(&ic_msg, !success);"] %}

### block before
  /* Iterate through all images currently in memory */
###   if target == "darwin"
  const int image_count_before = _dyld_image_count();
  const char ** images_before = alloca(image_count_before * sizeof(char*));
  collect_loaded_image_names(images_before, image_count_before);
###  else
  int image_count_before = 0;
  dl_iterate_phdr(count_shared_libs_cb, &image_count_before);
  const char ** images_before = alloca(image_count_before * sizeof(char*));
  shared_libs_as_char_array_cb_data_t cb_data_before = {images_before, 0, image_count_before};
  dl_iterate_phdr(shared_libs_as_char_array_cb, &cb_data_before);
###  endif
  /* Release lock to allow intercepting shared library constructors. */
  if (i_locked) {
    release_global_lock();
  }
### endblock before

### block after
  if (i_am_intercepting) {
    grab_global_lock(&i_locked, "{{ func }}");
  }
###   if target == "darwin"
  const int image_count_after = _dyld_image_count();
  const char ** images_after = alloca(image_count_after * sizeof(char*));
  collect_loaded_image_names(images_after, image_count_after);
###   else
  int image_count_after = 0;
  dl_iterate_phdr(count_shared_libs_cb, &image_count_after);
  const char ** images_after = alloca(image_count_after * sizeof(char*));
  shared_libs_as_char_array_cb_data_t cb_data_after = {images_after, 0, image_count_after};
  dl_iterate_phdr(shared_libs_as_char_array_cb, &cb_data_after);
###   endif
  size_t new_libs_count = 0;
  /* Allocate space for the worst case, i.e. all the loaded shared libs are new, which is highly
   * unlikely. */
  const char ** new_libs = alloca(image_count_after * sizeof(char*));
  newly_loaded_images(images_before, image_count_before, images_after, image_count_after,
                      new_libs, &new_libs_count);
  size_t i;
  for (i = 0; i < new_libs_count; i++) {
    const int orig_len = strlen(new_libs[i]);
    if (!is_canonical(new_libs[i], orig_len)) {
        /* Don't use strdupa(), because it does not exist on macOS. */
      char* new_lib = alloca(orig_len + 1);
      memcpy(new_lib, new_libs[i], orig_len + 1);
      make_canonical(new_lib, orig_len + 1);
      new_libs[i] = new_lib;
    }
  }
### endblock after
