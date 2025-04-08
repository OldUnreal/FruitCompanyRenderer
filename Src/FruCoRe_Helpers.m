#import <QuartzCore/CAMetalLayer.h>
#import "FruCoRe_Helpers.h"

void SetMetalVSync(void* Layer, unsigned char Enabled)
{
	if (!Layer) return;
	CAMetalLayer* InternalLayer = (__bridge CAMetalLayer*)Layer;
	InternalLayer.displaySyncEnabled = Enabled ? YES : NO;
}
