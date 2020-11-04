/*
  Copyright John Schember <john@nachtimwald.com>

  Permission is hereby granted, free of charge, to any person obtaining a copy of
  this software and associated documentation files (the "Software"), to deal in
  the Software without restriction, including without limitation the rights to
  use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies
  of the Software, and to permit persons to whom the Software is furnished to do
  so, subject to the following conditions:

  The above copyright notice and this permission notice shall be included in all
  copies or substantial portions of the Software.

  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
  OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
  SOFTWARE.

  From:
  https://nachtimwald.com/2017/09/24/hex-encode-and-decode-in-c/
  https://nachtimwald.com/legal/
 */

char *bin2hex(const unsigned char *bin, size_t len)
{
	char   *out;
	size_t  i;

	if (bin == NULL || len == 0)
		return NULL;

	out = malloc(len*2+1);
	for (i=0; i<len; i++) {
		out[i*2]   = "0123456789ABCDEF"[bin[i] >> 4];
		out[i*2+1] = "0123456789ABCDEF"[bin[i] & 0x0F];
	}
	out[len*2] = '\0';

	return out;
}
