#define FLIPBROWSER_HOST_TEST 1
#include "../native/flipbrowser.c"
#include <assert.h>
int main(int argc, char** argv) {
    BrowserState* s=(BrowserState*)calloc(1,sizeof(BrowserState));
    ANativeActivity a;
    int i,old_scroll,steps;
    uint32_t seed=1;
    assert(argc==2 && s); memset(&a,0,sizeof(a)); a.sdkVersion=9; s->activity=&a;
    s->item_count=100;
    for (i=0;i<52;++i) copy_text(s->items[i].path,argv[1],MAX_PATH);
    s->animate_previews=1;
    for(i=0;i<52;++i) load_one_thumbnail(s,0,51);
    for(i=0;i<52;++i) assert(s->items[i].animation && s->items[i].thumb);
    browser_set_animation(s,0);
    assert(s->thumb_count==0);
    for(i=0;i<52;++i) assert(!s->items[i].animation && !s->items[i].thumb);
    for(i=0;i<52;++i) load_one_thumbnail(s,0,51);
    for(i=0;i<52;++i) assert(!s->items[i].animation && s->items[i].thumb);
    puts("PASS static preview toggle frees all 52 complete PPM buffers and decoded layers");
    s->redraw=0; animate_visible_thumbnails(s,0,51,1234567); assert(!s->redraw);
    browser_set_animation(s,1);
    load_one_thumbnail(s,0,51);assert(s->items[0].animation);
    s->fling_velocity=1000; s->redraw=0;
    animate_visible_thumbnails(s,0,51,1234567);assert(!s->redraw);
    load_one_thumbnail(s,0,51);assert(!s->items[1].thumb);
    s->fling_velocity=0;
    puts("PASS no animation decode/load during scroll or fling");
    s->scroll=100;browser_fling_start(s,2000,1000);
    steps=0; old_scroll=s->scroll;
    while(s->fling_velocity && steps++<250) {
        browser_fling_tick(s,1000+(uint32_t)steps*16,10000);
        assert(s->scroll>=old_scroll && s->scroll<=10000);old_scroll=s->scroll;
    }
    assert(!s->fling_velocity && s->scroll>500 && steps<100);
    browser_fling_start(s,-10000,1000);browser_fling_tick(s,1150,10000);
    assert(s->scroll==0 && !s->fling_velocity);
    s->scroll=9999;browser_fling_start(s,10000,1000);browser_fling_tick(s,1016,10000);
    assert(s->scroll==10000 && !s->fling_velocity);
    s->scroll=100;browser_fling_start(s,5000,1000);browser_fling_tick(s,4000,10000);
    assert(s->scroll==100 && !s->fling_velocity);
    puts("PASS fling deceleration, both edges, no position jump after long suspension");
    s->sample_count=0;browser_sample(s,300,1000);browser_sample(s,200,1050);browser_sample(s,100,1100);
    assert(browser_release_velocity(s,1100)==2000);
    browser_sample(s,100,1400);assert(browser_release_velocity(s,1400)==0);
    s->scroll=100;browser_fling_start(s,1000,2000);
    browser_handle_motion(s,0,0,150,300,320,480,2010);
    assert(!s->fling_velocity && s->touching && s->suppress_tap);
    browser_handle_motion(s,0,1,150,300,320,480,2050); assert(!s->finishing);
    browser_handle_motion(s,0,0,150,300,320,480,3000);
    browser_handle_motion(s,0,2,150,200,320,480,3050);
    browser_handle_motion(s,0,3,150,200,320,480,3080);
    assert(!s->touching && !s->fling_velocity);
    puts("PASS held finger prevents fling; touch brakes without opening a file; CANCEL stops");
    for(i=0;i<10000;++i){
        uint64_t ns;
        seed=seed*1664525u+1013904223u;ns=(uint64_t)seed<<32;
        seed=seed*1664525u+1013904223u;ns|=seed;ns&=0x7fffffffffffffffULL;
        assert(browser_event_ms((int64_t)ns)==(uint32_t)(ns/1000000u));
    }
    puts("PASS 10,000 nanosecond timestamp conversions, including wrap-around");
    free_thumbnails(s);free(s);return 0;
}
