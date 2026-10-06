#include "codeck_page.h"
#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include "rlcd.h"
#include "brand_icons.h"
#include "codeck_label.h"

static const char *status_label(codeck_status_t s)
{
    switch(s) {
    case CODECK_OK: return "ONLINE";
    case CODECK_WAIT_NETWORK: return "OFFLINE";
    case CODECK_WAIT_TIME: return "WAITING FOR SNTP";
    case CODECK_NO_KEY: return "NO DEVICE KEY";
    case CODECK_AUTH_ERROR: return "401 AUTH ERROR";
    case CODECK_SERVICE_ERROR: return "503 SERVICE UNAVAILABLE";
    case CODECK_ACCESS_DENIED: return "403 ACCESS DENIED";
    case CODECK_TLS_ERROR: return "TLS CONNECTION ERROR";
    case CODECK_DNS_ERROR: return "DNS ERROR";
    case CODECK_CONNECT_TIMEOUT: return "CONNECTION TIMEOUT";
    case CODECK_JSON_ERROR: return "INCOMPLETE OR INVALID JSON";
    case CODECK_UNSUPPORTED_VERSION: return "UNSUPPORTED API VERSION";
    case CODECK_CAPACITY_ERROR: return "SNAPSHOT CAPACITY ERROR";
    default: return "NETWORK ERROR";
    }
}
static void rounded_card(int x,int y,int width,int height)
{
    rlcd_hline(x+4,y,width-8); rlcd_hline(x+4,y+height-1,width-8);
    for(int row=1;row<height-1;++row) {
        const int inset=row==1 || row==height-2 ? 2 : row==2 || row==height-3 ? 1 : 0;
        rlcd_hline(x+inset,y+row,1); rlcd_hline(x+width-1-inset,y+row,1);
    }
}
static void time_text(const char *utc, char *out, size_t capacity, bool full)
{
    if(!utc[0]) { snprintf(out,capacity,"--"); return; }
    snprintf(out,capacity,full ? "%.10s %.5s UTC" : "%.5s %.8s UTC",utc+(full ? 0 : 5),utc+11);
}
static unsigned account_sheets(const codeck_account_t *a)
{
    return a->observed && a->amount_count ? (a->amount_count+1)/2 : 1;
}
unsigned codeck_page_count(const codeck_snapshot_t *s)
{
    unsigned count=1;
    if(s->has_snapshot && s->balances_available && s->account_count) {
        for(unsigned i=0;i<s->account_count;++i) count+=account_sheets(&s->accounts[i]);
    } else ++count;
    return count;
}
static void quota(int x, const codeck_window_t *w)
{
    char text[64];
    rounded_card(x,144,174,111);
    rlcd_text(x+10,154,x==20 ? "PRIMARY" : "SECONDARY",2);
    if(!w->valid) {
        rlcd_text(x+10,180,"--",4); rlcd_text(x+10,226,"UNAVAILABLE",1); return;
    }
    snprintf(text,sizeof(text),"%.0f%% LEFT",100-w->used_percent);
    rlcd_text(x+10,179,text,3);
    snprintf(text,sizeof(text),"%.1f%% USED",w->used_percent); rlcd_text(x+10,207,text,1);
    rlcd_rect(x+10,222,154,7);
    const int width=(int)((100-w->used_percent)*150/100);
    for(int y=224;y<227;++y) rlcd_hline(x+12,y,width);
    char reset[32]; time_text(w->resets_at,reset,sizeof(reset),true);
    snprintf(text,sizeof(text),"RESET %s",reset); rlcd_text(x+10,241,text,1);
}
static void overview(const codeck_snapshot_t *s)
{
    char text[48];
    const bool cached=s->status!=CODECK_OK;
    rlcd_text(20,72,!s->has_snapshot ? "SERVICE --" : s->service_available ? "SERVICE OK" : "SERVICE DOWN",2);
    rlcd_text(224,72,!s->has_snapshot ? "CLI --" : s->codex_cli_available ? "CLI OK" : "CLI DOWN",2);
    rlcd_text(20,94,!s->has_snapshot ? "SCHEDULER --" : s->scheduler_enabled ? "SCHEDULER ON" : "SCHEDULER OFF",2);
    if(s->has_snapshot) snprintf(text,sizeof(text),"%s %u",cached ? "LAST" : "RUN",s->running_tasks);
    else strcpy(text,"RUN --");
    rlcd_text(224,94,text,strlen(text)>13 ? 1 : 2);
    brand_icon_draw(BRAND_ICON_CODEX_TERMINAL,20,114,24); rlcd_text(52,120,"CODEX QUOTA",2);
    char obs[32]; time_text(s->quota_observed_at,obs,sizeof(obs),false);
    snprintf(text,sizeof(text),"OBS %s",obs); rlcd_text(218,123,text,1);
    codeck_window_t empty={0};
    quota(20,s->quota_available ? &s->windows[0] : &empty);
    quota(206,s->quota_available ? &s->windows[1] : &empty);
}
static void amount_row(int y,const codeck_amount_t *v)
{
    char formatted[72],text[88];
    if(!codeck_amount_format(v->amount,formatted,sizeof(formatted))) strcpy(formatted,"--");
    snprintf(text,sizeof(text),"%s %s",v->currency,formatted);
    unsigned scale=strlen(text)<=19 ? 3 : strlen(text)<=28 ? 2 : 1;
    if(strlen(text)<=56) rlcd_text(30,y,text,scale);
    else {
        char first[57]; memcpy(first,text,56); first[56]=0;
        rlcd_text(30,y,first,1); rlcd_text(30,y+12,text+56,1);
    }
}
static void balance(const codeck_snapshot_t *s,unsigned sheet)
{
    if(!s->has_snapshot || !s->balances_available || !s->account_count) {
        rounded_card(20,75,360,177); rlcd_text(34,99,"BALANCES",2); rlcd_text(34,133,"--",4);
        rlcd_text(34,180,!s->has_snapshot ? "NO SUCCESSFUL SNAPSHOT" : !s->balances_available ? "BALANCES UNAVAILABLE" : "NO BALANCE CONFIGS",2);
        return;
    }
    unsigned offset=sheet-1,index=0;
    while(index+1<s->account_count && offset>=account_sheets(&s->accounts[index])) {
        offset-=account_sheets(&s->accounts[index++]);
    }
    const codeck_account_t *a=&s->accounts[index];
    rounded_card(20,72,360,184);
    brand_icon_t icon=strcmp(a->provider,"deepseek")==0 ? BRAND_ICON_DEEPSEEK :
                      strcmp(a->provider,"openrouter")==0 ? BRAND_ICON_OPENROUTER : BRAND_ICON_COUNT;
    brand_icon_draw(icon,30,82,24);
    char provider[32];
    for(unsigned i=0;i<sizeof(provider);++i) { provider[i]=(char)toupper((unsigned char)a->provider[i]); if(!provider[i]) break; }
    rlcd_text(64,82,provider,strlen(provider)>15 ? 1 : 2);
    // Name renderer supports UTF-8 account labels; full source stays in snapshot.
    codeck_label_text(30,108,a->name,340);
    const bool known=a->observed && a->amount_count;
    if(known) {
        unsigned first=offset*2;
        amount_row(137,&a->amounts[first]);
        if(first+1<a->amount_count) amount_row(174,&a->amounts[first+1]);
    } else { rlcd_text(30,141,"--",4); rlcd_text(30,181,"NO SUCCESSFUL OBSERVATION",1); }
    char observed[40],text[64]; time_text(a->observed_at,observed,sizeof(observed),true);
    snprintf(text,sizeof(text),"UPDATED %s",observed); rlcd_text(30,225,text,1);
    rlcd_text(30,242,!known ? "NO DATA" : a->stale ? "OLD DATA" : "CURRENT",1);
    if(s->status!=CODECK_OK && s->has_snapshot) rlcd_text(140,242,"CACHED SNAPSHOT",1);
    snprintf(text,sizeof(text),"ACCOUNT %u OF %u",index+1,s->account_count); rlcd_text(260,242,text,1);
}
void draw_codeck_page(const codeck_snapshot_t *s,unsigned sheet)
{
    char text[64],time[32];
    sheet%=codeck_page_count(s);
    rlcd_text(20,10,"CODECK",3);
    rlcd_text(284,16,s->has_snapshot ? s->status==CODECK_OK ? "LIVE DATA" : "LAST KNOWN" : "NO SNAPSHOT",1);
    rlcd_text(20,39,status_label(s->status),2); rlcd_hline(20,61,360);
    if(sheet==0) overview(s); else balance(s,sheet);
    rlcd_hline(20,265,360);
    time_text(s->generated_at,time,sizeof(time),true);
    snprintf(text,sizeof(text),"%s %s",s->has_snapshot && s->status!=CODECK_OK ? "CACHED" : "SNAPSHOT",time);
    rlcd_text(20,273,text,1);
    snprintf(text,sizeof(text),"2 OF 4  CODECK %u OF %u",sheet+1,codeck_page_count(s)); rlcd_text(20,288,text,1);
    rlcd_text(224,288,"12S AUTO  KEY NEXT",1);
}
