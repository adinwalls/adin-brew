// minimal LV2 host: lv2host URI in.wav out.wav [symbol=value ...]
#include <lilv/lilv.h>
#include <lv2/urid/urid.h>
#include <lv2/options/options.h>
#include <lv2/buf-size/buf-size.h>
#include <lv2/atom/atom.h>
#include <lv2/parameters/parameters.h>
#include <sndfile.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static char* uris[256]; static int nuris=0;
static LV2_URID map_uri(LV2_URID_Map_Handle h,const char* u){for(int i=0;i<nuris;i++) if(!strcmp(uris[i],u)) return i+1; uris[nuris]=strdup(u); return ++nuris;}
static const char* unmap_uri(LV2_URID_Unmap_Handle h,LV2_URID id){return id&&id<=nuris?uris[id-1]:NULL;}
int main(int argc,char**argv){
  LilvWorld* w=lilv_world_new(); lilv_world_load_all(w);
  LilvNode* uri=lilv_new_uri(w,argv[1]); const LilvPlugin* p=lilv_plugins_get_by_uri(lilv_world_get_all_plugins(w),uri);
  if(!p){fprintf(stderr,"plugin not found\n");return 1;}
  SF_INFO si={0}; SNDFILE* in=sf_open(argv[2],SFM_READ,&si); int ch=si.channels; long n=si.frames;
  float* buf=malloc(sizeof(float)*n*ch); sf_readf_float(in,buf,n); sf_close(in);
  LV2_URID_Map map={NULL,map_uri}; LV2_URID_Unmap unmap={NULL,unmap_uri};
  float srf=si.samplerate; int32_t blk=256;
  LV2_Options_Option opts[]={
    {LV2_OPTIONS_INSTANCE,0,map_uri(0,LV2_PARAMETERS__sampleRate),sizeof(float),map_uri(0,LV2_ATOM__Float),&srf},
    {LV2_OPTIONS_INSTANCE,0,map_uri(0,LV2_BUF_SIZE__maxBlockLength),sizeof(int32_t),map_uri(0,LV2_ATOM__Int),&blk},
    {LV2_OPTIONS_INSTANCE,0,map_uri(0,LV2_BUF_SIZE__minBlockLength),sizeof(int32_t),map_uri(0,LV2_ATOM__Int),&blk},
    {LV2_OPTIONS_INSTANCE,0,map_uri(0,LV2_BUF_SIZE__nominalBlockLength),sizeof(int32_t),map_uri(0,LV2_ATOM__Int),&blk},
    {LV2_OPTIONS_INSTANCE,0,0,0,0,NULL}};
  LV2_Feature fm={LV2_URID__map,&map},fu={LV2_URID__unmap,&unmap},fo={LV2_OPTIONS__options,opts},fb={LV2_BUF_SIZE__boundedBlockLength,NULL};
  const LV2_Feature* feats[]={&fm,&fu,&fo,&fb,NULL};
  LilvInstance* inst=lilv_plugin_instantiate(p,si.samplerate,feats); if(!inst){fprintf(stderr,"instantiate failed\n");return 1;}
  uint32_t np=lilv_plugin_get_num_ports(p); float ctl[64]; float ain[2][256],aout[2][256]; int ai=0,ao=0; int latPort=-1, hzPort=-1;
  LilvNode *audio=lilv_new_uri(w,LV2_CORE__AudioPort),*control=lilv_new_uri(w,LV2_CORE__ControlPort),*input=lilv_new_uri(w,LV2_CORE__InputPort);
  for(uint32_t i=0;i<np;i++){const LilvPort* port=lilv_plugin_get_port_by_index(p,i); const char* sym=lilv_node_as_string(lilv_port_get_symbol(p,port));
    if(lilv_port_is_a(p,port,audio)){ if(lilv_port_is_a(p,port,input)) lilv_instance_connect_port(inst,i,ain[ai++]); else lilv_instance_connect_port(inst,i,aout[ao++]); }
    else if(lilv_port_is_a(p,port,control)){ LilvNode* d; lilv_port_get_range(p,port,&d,NULL,NULL); ctl[i]=d?lilv_node_as_float(d):0;
      for(int a=4;a<argc;a++){char s[64];float v; if(sscanf(argv[a],"%63[^=]=%f",s,&v)==2&&!strcmp(s,sym)) ctl[i]=v;}
      if(!strcmp(sym,"lv2_latency")) latPort=i; if(!strcmp(sym,"detected_hz")) hzPort=i;
      lilv_instance_connect_port(inst,i,&ctl[i]); }
    else lilv_instance_connect_port(inst,i,NULL);}
  lilv_instance_activate(inst);
  float* out=calloc(n*2,sizeof(float)); int hzcount=0;
  for(long s=0;s<n;s+=256){int fr=n-s<256?n-s:256; for(int k=0;k<fr;k++)for(int c=0;c<2;c++) ain[c][k]=buf[(s+k)*ch+(ch>1?c:0)];
    lilv_instance_run(inst,fr); for(int k=0;k<fr;k++)for(int c=0;c<2;c++) out[(s+k)*2+c]=aout[c][k]; if(hzPort>=0&&ctl[hzPort]>0) hzcount++;}
  fprintf(stderr,"ports %u, reported latency %.0f, blocks with detected pitch %d\n",np,latPort>=0?ctl[latPort]:-1,hzcount);
  lilv_instance_deactivate(inst); lilv_instance_free(inst);
  SF_INFO so={.samplerate=si.samplerate,.channels=2,.format=SF_FORMAT_WAV|SF_FORMAT_FLOAT}; SNDFILE* o=sf_open(argv[3],SFM_WRITE,&so); sf_writef_float(o,out,n); sf_close(o);
  return 0;}
