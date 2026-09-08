#include <stddef.h>
#include <stdint.h>
typedef struct A A; typedef struct W W; typedef struct Q Q; typedef struct E E;
typedef struct B {int32_t a,b,c,d;void*e;uint32_t f[6];} B;
void ANativeActivity_finish(A*a){} void ANativeActivity_setWindowFormat(A*a,int32_t b){}
int32_t ANativeWindow_lock(W*a,B*b,void*c){return 0;} int32_t ANativeWindow_unlockAndPost(W*a){return 0;}
int32_t AInputQueue_getEvent(Q*a,E**b){return -1;} int32_t AInputQueue_preDispatchEvent(Q*a,E*b){return 0;}
void AInputQueue_finishEvent(Q*a,E*b,int c){} int32_t AInputEvent_getType(const E*a){return 0;}
int32_t AKeyEvent_getAction(const E*a){return 0;} int32_t AKeyEvent_getKeyCode(const E*a){return 0;}
int32_t AMotionEvent_getAction(const E*a){return 0;} float AMotionEvent_getX(const E*a,size_t b){return 0;}
float AMotionEvent_getY(const E*a,size_t b){return 0;}
