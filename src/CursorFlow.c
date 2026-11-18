// CursorFlow: native AppKit application. Runtime calls avoid a build-time SDK dependency.
#include <dlfcn.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <math.h>

typedef void *ID;
typedef void *SEL;
typedef long Int;
typedef unsigned long UInt;
typedef struct { double x, y; } Point;
typedef struct { double width, height; } Size;
typedef struct { Point origin; Size size; } Rect;
static void *(*getClass)(const char *);
static SEL (*selector)(const char *);
static void *message;
static void *(*allocateClass)(void *,const char *,size_t);
static void (*registerClass)(void *);
static bool (*addMethod)(void *,SEL,void (*)(void),const char *);
#define CLS(n) getClass(n)
#define S(n) selector(n)
#define M0(T,o,s) ((T (*)(ID,SEL))message)((o),S(s))
#define M1(T,o,s,A,a) ((T (*)(ID,SEL,A))message)((o),S(s),(a))
#define M2(T,o,s,A,a,B,b) ((T (*)(ID,SEL,A,B))message)((o),S(s),(a),(b))
#define M3(T,o,s,A,a,B,b,C,c) ((T (*)(ID,SEL,A,B,C))message)((o),S(s),(a),(b),(c))
#define M4(T,o,s,A,a,B,b,C,c,D,d) ((T (*)(ID,SEL,A,B,C,D))message)((o),S(s),(a),(b),(c),(d))
static ID str(const char *s) { return M1(ID,CLS("NSString"),"stringWithUTF8String:",const char*,s); }
static ID make(const char *name) { return M0(ID,M0(ID,CLS(name),"alloc"),"init"); }
static void text(ID o,const char *s) { M1(void,o,"setTitle:",ID,str(s)); }
static void frame(ID o,double x,double y,double w,double h) {
    Rect r={{x,y},{w,h}}; M1(void,o,"setFrame:",Rect,r);
}
static ID app,delegate,menuItem,menuStatus,menuToggle,window,statusLabel,intervalLabel,slider,startButton,defaults;
static ID choices[6],timer,permissionLabel,permissionTimer;
static int seconds=10;
static bool running=false,moving=false;
static Point expected,startPoint,target;
static double deadline,started,duration;
static bool (*trusted)(void);
static bool (*requestTrust)(ID);
static void *(*eventCreate)(void *);
static Point (*eventLocation)(void *);
static void *(*mouseEvent)(void *,uint32_t,Point,uint32_t);
static void (*postEvent)(uint32_t,void *);
static void (*releaseCF)(void *);
static bool (*keyState)(int,uint16_t);
static bool (*buttonState)(int,uint32_t);
static uint32_t (*mainDisplay)(void);
static Rect (*displayBounds)(uint32_t);
static int (*displaysAt)(Point,uint32_t,uint32_t *,uint32_t *);

static double now(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC,&t); return t.tv_sec+t.tv_nsec/1e9; }
static double randomUnit(void) { return (double)arc4random()/4294967295.0; }
static double nextDelay(void) { return seconds*(1.0+randomUnit()); }
static double distance(Point a,Point b) { return hypot(a.x-b.x,a.y-b.y); }
static bool cursor(Point *p) {
    void *e=eventCreate(NULL); if(!e) return false;
    *p=eventLocation(e); releaseCF(e); return true;
}
static void showWindow(ID self,SEL cmd,ID sender) {
    (void)self;(void)cmd;(void)sender;
    M1(void,app,"activateIgnoringOtherApps:",bool,true);
    M1(void,window,"makeKeyAndOrderFront:",ID,NULL);
}
static void privacy(ID self,SEL cmd,ID sender) {
    (void)self;(void)cmd;(void)sender;
    if(!trusted()) {
        ID yes=M1(ID,CLS("NSNumber"),"numberWithBool:",bool,true);
        ID options=M2(ID,CLS("NSDictionary"),"dictionaryWithObject:forKey:",ID,yes,ID,str("AXTrustedCheckOptionPrompt"));
        requestTrust(options);
    }
    ID url=M1(ID,CLS("NSURL"),"URLWithString:",ID,str("x-apple.systempreferences:com.apple.preference.security?Privacy_Accessibility"));
    M1(bool,M0(ID,CLS("NSWorkspace"),"sharedWorkspace"),"openURL:",ID,url);
}
static void revealApp(ID self,SEL cmd,ID sender) {
    (void)self;(void)cmd;(void)sender;
    ID url=M0(ID,M0(ID,CLS("NSBundle"),"mainBundle"),"bundleURL");
    ID urls=M1(ID,CLS("NSArray"),"arrayWithObject:",ID,url);
    M1(void,M0(ID,CLS("NSWorkspace"),"sharedWorkspace"),"activateFileViewerSelectingURLs:",ID,urls);
}
static void security(ID self,SEL cmd,ID sender) {
    (void)self;(void)cmd;(void)sender;
    ID url=M1(ID,CLS("NSURL"),"URLWithString:",ID,str("x-apple.systempreferences:com.apple.preference.security"));
    M1(bool,M0(ID,CLS("NSWorkspace"),"sharedWorkspace"),"openURL:",ID,url);
}
static void refresh(void) {
    bool granted=trusted();
    const char *state=running?"Работает • Esc — остановить":(granted?"Готово к запуску":"Нужно разрешить управление курсором");
    if(permissionLabel) M1(void,permissionLabel,"setStringValue:",ID,str(granted?"✓ Разрешение macOS выдано":"Включите CursorFlow в «Универсальном доступе»."));
    if(statusLabel) M1(void,statusLabel,"setStringValue:",ID,str(state));
    if(menuStatus) text(menuStatus,running?"CursorFlow • работает":"CursorFlow • остановлено");
    if(menuToggle) text(menuToggle,running?"Остановить":"Запустить");
    if(startButton) text(startButton,running?"Остановить":(granted?"Запустить":"Выдать разрешение"));
    if(menuItem) {
        ID button=M0(ID,menuItem,"button");
        text(button,running?" •":"");
        M1(void,button,"setToolTip:",ID,str(running?"CursorFlow — работает":"CursorFlow — остановлено"));
    }
}
static void pollPermission(ID self,SEL cmd,ID sender) {
    (void)self;(void)cmd;(void)sender;
    static int previous=-1;
    int granted=trusted()?1:0;
    if(granted!=previous) { previous=granted; refresh(); }
}
static void stop(void) {
    running=false; moving=false;
    if(timer) { M0(void,timer,"invalidate"); timer=NULL; }
    refresh();
}
static void updateInterval(void) {
    char buf[100]; snprintf(buf,sizeof buf,"Интервал: %d–%d секунд",seconds,seconds*2);
    if(intervalLabel) M1(void,intervalLabel,"setStringValue:",ID,str(buf));
    if(slider) M1(void,slider,"setDoubleValue:",double,(double)seconds);
    int values[]={2,5,10,20,30,60};
    for(int i=0;i<6;i++) if(choices[i]) M1(void,choices[i],"setState:",Int,(Int)(seconds==values[i]));
    M2(void,defaults,"setInteger:forKey:",Int,(Int)seconds,ID,str("interval"));
    if(running&&!moving) deadline=now()+nextDelay();
}
static void sliderChanged(ID self,SEL cmd,ID sender) {
    (void)self;(void)cmd;
    seconds=(int)lround(M0(double,sender,"doubleValue")); updateInterval();
}
static void selectInterval(ID self,SEL cmd,ID sender) {
    (void)self;(void)cmd;
    seconds=(int)M0(Int,sender,"tag"); updateInterval();
}
static void tick(ID self,SEL cmd,ID sender) {
    (void)self;(void)cmd;(void)sender;
    if(!running) return;
    if(!trusted()||keyState(0,53)) { stop(); return; }
    Point p; if(!cursor(&p)) { stop(); return; }
    double t=now();
    // Physical movement or held mouse buttons always takes priority.
    if(distance(p,expected)>(moving?8.0:3.0)||buttonState(0,0)||buttonState(0,1)||buttonState(0,2)) {
        moving=false; expected=p; deadline=t+nextDelay(); return;
    }
    if(!moving) {
        if(t<deadline) return;
        uint32_t displays[16],count=0,display=mainDisplay();
        if(displaysAt(p,16,displays,&count)==0&&count) display=displays[0];
        Rect bounds=displayBounds(display);
        if(bounds.size.width<=100||bounds.size.height<=100) { deadline=t+nextDelay(); return; }
        target.x=bounds.origin.x+50+randomUnit()*(bounds.size.width-100);
        target.y=bounds.origin.y+50+randomUnit()*(bounds.size.height-100);
        startPoint=p; started=t; duration=0.8+1.2*randomUnit(); moving=true;
    }
    double f=fmin(1.0,(t-started)/duration), ease=f*f*(3.0-2.0*f);
    Point next={startPoint.x+(target.x-startPoint.x)*ease,startPoint.y+(target.y-startPoint.y)*ease};
    void *event=mouseEvent(NULL,5,next,0);
    if(!event) { stop(); return; }
    postEvent(0,event); releaseCF(event); expected=next;
    if(f>=1.0) { moving=false; deadline=t+nextDelay(); }
}
static void toggle(ID self,SEL cmd,ID sender) {
    (void)self;(void)cmd;(void)sender;
    if(running) { stop(); return; }
    if(!trusted()) { privacy(NULL,NULL,NULL); refresh(); return; }
    if(!cursor(&expected)) return;
    running=true; moving=false; deadline=now()+nextDelay();
    timer=((ID(*)(ID,SEL,double,ID,SEL,ID,bool))message)(CLS("NSTimer"),S("scheduledTimerWithTimeInterval:target:selector:userInfo:repeats:"),1.0/30.0,delegate,S("tick:"),NULL,true);
    refresh();
}
static void quit(ID self,SEL cmd,ID sender) {
    (void)self;(void)cmd;(void)sender; stop(); M1(void,app,"terminate:",ID,NULL);
}
static ID item(ID menu,const char *title,const char *action) {
    ID it=M3(ID,menu,"addItemWithTitle:action:keyEquivalent:",ID,str(title),SEL,action?S(action):NULL,ID,str(""));
    if(action) M1(void,it,"setTarget:",ID,delegate);
    return it;
}
static void separator(ID menu) { M1(void,menu,"addItem:",ID,M0(ID,CLS("NSMenuItem"),"separatorItem")); }
static ID label(ID content,const char *s,double x,double y,double w,double h) {
    ID v=M1(ID,CLS("NSTextField"),"labelWithString:",ID,str(s));
    frame(v,x,y,w,h); M1(void,content,"addSubview:",ID,v); return v;
}
static void launched(ID self,SEL cmd,ID notification) {
    (void)self;(void)cmd;(void)notification;
    defaults=M0(ID,CLS("NSUserDefaults"),"standardUserDefaults");
    Int saved=M1(Int,defaults,"integerForKey:",ID,str("interval"));
    if(saved>=2&&saved<=60) seconds=(int)saved;
    Rect r={{0,0},{540,410}};
    window=M4(ID,M0(ID,CLS("NSWindow"),"alloc"),"initWithContentRect:styleMask:backing:defer:",Rect,r,UInt,7UL,UInt,2UL,bool,false);
    text(window,"CursorFlow"); M1(void,window,"setReleasedWhenClosed:",bool,false); M0(void,window,"center");
    ID content=M0(ID,window,"contentView");
    ID heading=label(content,"Случайное движение мыши",28,350,490,30);
    M1(void,heading,"setFont:",ID,M1(ID,CLS("NSFont"),"boldSystemFontOfSize:",double,21.0));
    statusLabel=label(content,"Остановлено",28,315,490,22);
    intervalLabel=label(content,"Интервал: 10–20 секунд",28,265,490,22);
    slider=((ID(*)(ID,SEL,double,double,double,ID,SEL))message)(CLS("NSSlider"),S("sliderWithValue:minValue:maxValue:target:action:"),(double)seconds,2.0,60.0,delegate,S("sliderChanged:"));
    frame(slider,26,228,488,24); M1(void,content,"addSubview:",ID,slider);
    startButton=M3(ID,CLS("NSButton"),"buttonWithTitle:target:action:",ID,str("Запустить"),ID,delegate,SEL,S("toggle:"));
    frame(startButton,26,178,190,32); M1(void,content,"addSubview:",ID,startButton);
    permissionLabel=label(content,"Включите CursorFlow в «Универсальном доступе».",28,135,490,22);
    ID allowButton=M3(ID,CLS("NSButton"),"buttonWithTitle:target:action:",ID,str("Открыть разрешения"),ID,delegate,SEL,S("privacy:"));
    frame(allowButton,26,88,205,32); M1(void,content,"addSubview:",ID,allowButton);
    ID locateButton=M3(ID,CLS("NSButton"),"buttonWithTitle:target:action:",ID,str("Показать приложение"),ID,delegate,SEL,S("revealApp:"));
    frame(locateButton,245,88,215,32); M1(void,content,"addSubview:",ID,locateButton);
    ID hint=label(content,"Esc — остановить. Ручное движение прерывает автоматику.",28,46,490,20);
    M1(void,hint,"setFont:",ID,M1(ID,CLS("NSFont"),"systemFontOfSize:",double,11.0));
    label(content,"Клики и нажатия клавиш не выполняются.",28,20,490,20);
    menuItem=M1(ID,M0(ID,CLS("NSStatusBar"),"systemStatusBar"),"statusItemWithLength:",double,-1.0);
    M0(ID,menuItem,"retain");
    ID button=M0(ID,menuItem,"button");
    ID bundle=M0(ID,CLS("NSBundle"),"mainBundle");
    ID path=M2(ID,bundle,"pathForResource:ofType:",ID,str("TrayIcon"),ID,str("png"));
    ID image=M1(ID,M0(ID,CLS("NSImage"),"alloc"),"initWithContentsOfFile:",ID,path);
    if(image) {
        Size size={18,18}; M1(void,image,"setSize:",Size,size); M1(void,image,"setTemplate:",bool,true);
        M1(void,button,"setImage:",ID,image); M1(void,button,"setImagePosition:",UInt,2UL); M0(void,image,"release");
    } else text(button,"↗︎");
    ID menu=make("NSMenu");
    M1(void,menu,"setAutoenablesItems:",bool,false);
    menuStatus=item(menu,"CursorFlow • остановлено",NULL); M1(void,menuStatus,"setEnabled:",bool,false);
    separator(menu); menuToggle=item(menu,"Запустить","toggle:");
    ID submenu=make("NSMenu");
    int values[]={2,5,10,20,30,60}; char buf[100];
    for(int i=0;i<6;i++) {
        snprintf(buf,sizeof buf,"%d–%d секунд",values[i],values[i]*2);
        choices[i]=item(submenu,buf,"selectInterval:"); M1(void,choices[i],"setTag:",Int,(Int)values[i]);
    }
    M1(void,item(menu,"Интервал",NULL),"setSubmenu:",ID,submenu);
    item(menu,"Настройки…","showWindow:");
    item(menu,"Открыть разрешения macOS…","privacy:");
    item(menu,"Показать CursorFlow в Finder","revealApp:");
    item(menu,"Конфиденциальность и безопасность…","security:");
    separator(menu); item(menu,"Завершить CursorFlow","quit:");
    M1(void,menuItem,"setMenu:",ID,menu);
    updateInterval(); refresh(); showWindow(NULL,NULL,NULL);
    permissionTimer=((ID(*)(ID,SEL,double,ID,SEL,ID,bool))message)(CLS("NSTimer"),S("scheduledTimerWithTimeInterval:target:selector:userInfo:repeats:"),1.0,delegate,S("pollPermission:"),NULL,true);
    if(!trusted()) privacy(NULL,NULL,NULL);
}
static bool reopen(ID self,SEL cmd,ID appArg,bool visible) {
    (void)self;(void)cmd;(void)appArg;(void)visible; showWindow(NULL,NULL,NULL); return true;
}
static bool installMethod(void *c,const char *name,void (*imp)(void),const char *types) {
    return addMethod(c,S(name),imp,types);
}
#define LOAD(handle,variable,symbol) do { *(void **)(&(variable))=dlsym((handle),(symbol)); if(!(variable)) { fprintf(stderr,"Missing symbol: %s\n",(symbol)); return 1; } } while(0)
int main(void) {
    void *cocoa=dlopen("/System/Library/Frameworks/Cocoa.framework/Cocoa",RTLD_NOW|RTLD_GLOBAL);
    void *objc=dlopen("/usr/lib/libobjc.A.dylib",RTLD_NOW|RTLD_GLOBAL);
    void *ax=dlopen("/System/Library/Frameworks/ApplicationServices.framework/ApplicationServices",RTLD_NOW|RTLD_GLOBAL);
    void *cg=dlopen("/System/Library/Frameworks/CoreGraphics.framework/CoreGraphics",RTLD_NOW|RTLD_GLOBAL);
    void *cf=dlopen("/System/Library/Frameworks/CoreFoundation.framework/CoreFoundation",RTLD_NOW|RTLD_GLOBAL);
    if(!cocoa||!objc||!ax||!cg||!cf) return 1;
    LOAD(objc,getClass,"objc_getClass"); LOAD(objc,selector,"sel_registerName");
    message=dlsym(objc,"objc_msgSend"); if(!message) return 1;
    LOAD(objc,allocateClass,"objc_allocateClassPair"); LOAD(objc,registerClass,"objc_registerClassPair"); LOAD(objc,addMethod,"class_addMethod");
    LOAD(ax,trusted,"AXIsProcessTrusted"); LOAD(ax,requestTrust,"AXIsProcessTrustedWithOptions");
    LOAD(cg,eventCreate,"CGEventCreate"); LOAD(cg,eventLocation,"CGEventGetLocation"); LOAD(cg,mouseEvent,"CGEventCreateMouseEvent");
    LOAD(cg,postEvent,"CGEventPost"); LOAD(cf,releaseCF,"CFRelease");
    LOAD(cg,keyState,"CGEventSourceKeyState"); LOAD(cg,buttonState,"CGEventSourceButtonState");
    LOAD(cg,mainDisplay,"CGMainDisplayID"); LOAD(cg,displayBounds,"CGDisplayBounds"); LOAD(cg,displaysAt,"CGGetDisplaysWithPoint");
    ID pool=make("NSAutoreleasePool");
    app=M0(ID,CLS("NSApplication"),"sharedApplication");
    M1(bool,app,"setActivationPolicy:",Int,1L);
    void *class=allocateClass(CLS("NSObject"),"CursorFlowDelegate",0);
    if(!class) return 1;
    struct { const char *name; void (*imp)(void); const char *types; } methods[]={
        {"applicationDidFinishLaunching:",(void(*)(void))launched,"v@:@"},
        {"applicationShouldHandleReopen:hasVisibleWindows:",(void(*)(void))reopen,"B@:@B"},
        {"toggle:",(void(*)(void))toggle,"v@:@"}, {"tick:",(void(*)(void))tick,"v@:@"},
        {"showWindow:",(void(*)(void))showWindow,"v@:@"}, {"privacy:",(void(*)(void))privacy,"v@:@"},
        {"sliderChanged:",(void(*)(void))sliderChanged,"v@:@"}, {"selectInterval:",(void(*)(void))selectInterval,"v@:@"},
        {"pollPermission:",(void(*)(void))pollPermission,"v@:@"},
        {"revealApp:",(void(*)(void))revealApp,"v@:@"},
        {"security:",(void(*)(void))security,"v@:@"},
        {"quit:",(void(*)(void))quit,"v@:@"}
    };
    for(unsigned i=0;i<sizeof methods/sizeof methods[0];i++) if(!installMethod(class,methods[i].name,methods[i].imp,methods[i].types)) return 1;
    registerClass(class); delegate=make("CursorFlowDelegate"); M1(void,app,"setDelegate:",ID,delegate);
    M0(void,app,"run"); M0(void,pool,"drain"); return 0;
}
