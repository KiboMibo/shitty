// R10-qa. Один вопрос, на который весь разбор несимметричной перезагрузки
// опирается как на факт: чему равно layer.opaque у слоя, который никто
// не трогал? render_metal.mm ветвится ровно по нему, и если умолчание
// там NO, то ветка "окно плотное - опцию не слушаем" не срабатывает.
#import <Cocoa/Cocoa.h>
#import <QuartzCore/CAMetalLayer.h>

@interface ProbeView: NSView
@end

@implementation ProbeView
// Точная копия PltView.makeBackingLayer (platform_cocoa.mm:213).
- (CALayer*)makeBackingLayer {
    CAMetalLayer* layer = [CAMetalLayer layer];
    layer.needsDisplayOnBoundsChange = YES;
    return layer;
}
@end

int main() {
    @autoreleasepool {
        [NSApplication sharedApplication];
        NSWindow* window = [[NSWindow alloc]
            initWithContentRect:NSMakeRect(0, 0, 400, 300)
                      styleMask:(NSWindowStyleMaskTitled | NSWindowStyleMaskClosable | NSWindowStyleMaskResizable)
                        backing:NSBackingStoreBuffered
                          defer:NO];
        ProbeView* view = [[ProbeView alloc] initWithFrame:NSMakeRect(0, 0, 400, 300)];
        view.wantsLayer = YES;
        view.layerContentsRedrawPolicy = NSViewLayerContentsRedrawDuringViewResize;
        window.contentView = view;

        printf("bare CAMetalLayer .opaque      = %s\n", [CAMetalLayer layer].opaque ? "YES" : "NO");
        printf("contentView.layer class        = %s\n", NSStringFromClass([view.layer class]).UTF8String);
        printf("contentView.layer.opaque       = %s\n", view.layer.opaque ? "YES" : "NO");
        printf("contentView.isOpaque           = %s\n", view.isOpaque ? "YES" : "NO");
        printf("window.opaque                  = %s\n", window.opaque ? "YES" : "NO");
        printf("frame view exists              = %s\n", view.superview != nil ? "YES" : "NO");

        // И та же картина после того, как опция сработала бы.
        view.layer.opaque = NO;
        printf("after explicit NO: layer.opaque= %s\n", view.layer.opaque ? "YES" : "NO");
    }
    return 0;
}
