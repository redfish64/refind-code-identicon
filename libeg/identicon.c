#include "libegint.h"


static VOID drawRectangle(EG_IMAGE img, UINTN x, UINTN y, UINTN width, UINTN height,
			   EG_PIXEL color) {

  for (int i = x; i < x + width; i++) {
    if(i >= img.Width) break;

    for (int j = y; j < y+height; j++) {
      if (j >= img.Height) break;

      img.PixelData[img.Height * j + i] = color;
    }
  }
}
    

static VOID drawGrid(EG_IMAGE img, UINTN xi, UINTN yi, UINTN xis, UINTN yis, UINTN xs, UINTN ys,
		      UINTN xoff, UINTN yoff, EG_PIXEL color)
{
  UINTN x = xi * xs / xis + xoff;
  UINTN y = yi * ys / yis + yoff;
  UINTN xw = (xi+1) * xs / xis + xoff - x;
  UINTN yw = (yi+1) * ys / yis + yoff - y;

  drawRectangle(img, x, y, xw, yw, color);
}


static EG_PIXEL chooseColor(unsigned char* hash, int length, int mod, int off)
{
  UINT8 color[3] = {0,0,0};

  for(int i = 0; i < length; i+=3) {
    for(int j = 0; j < 3 && i+j < length; j++) {
	color[j] = color[j] ^ hash[i+j];
    }
  }

  for(int j = 0; j < 3; j++) 
    color[j] = (color[j] % mod) + off;

  EG_PIXEL c;
  c.b = color[0];
  c.g = color[1];
  c.r = color[2];
  c.a = 0;
  return c;
}



/**
 * Draw an identicon. Only up to the first 32 characters are used (enough to cover sha256).
 */
EG_IMAGE *egDrawIdenticon(IN UINTN IconSize, UINTN hashlen, unsigned char *hash) {
  if(hash == NULL) return NULL;

  UINTN w = IconSize;
  UINTN h = IconSize;

  EG_IMAGE *Image = egCreateImage(w,h, FALSE);
  if(Image == NULL) return NULL;

  //choose a random foregrond and background color. We choose a dark foreground and a light background,
  //but otherwise it's random)
  EG_PIXEL colors[2] = {chooseColor(hash,hashlen,63,128+64),
			chooseColor(hash,hashlen,92,0)};

  for (int i = 0; i < 32; i++) {
    
    //if the hash is too short, we wrap around. We don't want to recopy the exact same bytes,
    //so we xor with i to make it slightly different each time around
    unsigned char hi = hash[i % hashlen] ^ i;

    for(int j = 0; j < 8; j++) {
      int ci = (hi >> j) & 1;
      EG_PIXEL color = colors[ci];
      int index = (i << 3) | j;
      int x = index & 15;
      int y = index >> 4;
      //printf ("index is %d, x is %d, y is %d, ci is %d, hi is %d, i is %d, j is %d\n",index,x,y,ci,hi,i,j);
      
      //now we have an 16x16 position
      //we mirror the image left and right to make it more pretty for the user, creating a 32x16, which
      //we squish into the dimensions provided
      drawGrid(*Image, x, y, 16,16,w/2,h,0,0,color);
      drawGrid(*Image, 15-x, y, 16,16,w-w/2,h,w/2,0,color);

    }
  }

  return Image;
}
