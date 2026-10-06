#include "codeck_label.h"
#include "rlcd.h"
#include <stddef.h>

#ifdef CODECK_FONT_HOST
static const uint8_t *font;
void codeck_font_use(const uint8_t *data) { font=data; }
#else
extern const uint8_t font[] asm("_binary_codeck_labels_start");
#endif

void codeck_label_text(int x, int y, const char *utf8, int width)
{
    const int limit=x+width;
    const unsigned char *p=(const unsigned char *)utf8;
    while(*p) {
        uint32_t cp=*p++; int cell=8;
        if(cp>=0x80) {
            unsigned count=cp>=0xf0 ? 3 : cp>=0xe0 ? 2 : cp>=0xc0 ? 1 : 0;
            cp &= count==3 ? 7 : count==2 ? 15 : 31;
            for(unsigned i=0;i<count;++i) {
                if(!*p || (*p&0xc0)!=0x80) { cp=0xffff; break; }
                cp=(cp<<6)|(*p++&0x3f);
            }
            cell=16;
        }
        if(x+cell>limit) break;
        // Preserve the complete name in the model; long screen labels end ...
        if(*p && x+cell+16>limit) { rlcd_text(x,y+5,"...",1); break; }
        const uint8_t *glyph=NULL;
        if(cp<128) glyph=font+cp*32;
        else if(cp>=0x3000 && cp<0xa000) glyph=font+(128+cp-0x3000)*32;
        if(!glyph) rlcd_rect(x,y+2,12,12);
        else for(int row=0;row<16;++row) {
            int start=-1;
            for(int col=0;col<=cell;++col) {
                const int ink=col<cell && (glyph[row*2+col/8]&(0x80>>(col%8)));
                if(ink && start<0) start=col;
                if(!ink && start>=0) { rlcd_hline(x+start,y+row,col-start); start=-1; }
            }
        }
        x+=cell;
    }
}
