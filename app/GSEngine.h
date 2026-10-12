#import <Foundation/Foundation.h>
#import <UIKit/UIKit.h>
NS_ASSUME_NONNULL_BEGIN
@interface GSEngine : NSObject
// All methods must be called on the same serial background queue.
- (nullable NSDictionary<NSString *, id> *)reconstruct:(UIImage *)image
    models:(NSURL *)models output:(NSURL *)output backend:(NSString *)backend
    fov:(double)fov error:(NSError **)error NS_SWIFT_NAME(reconstruct(_:models:output:backend:fov:));
- (void)close;
@end
NS_ASSUME_NONNULL_END
