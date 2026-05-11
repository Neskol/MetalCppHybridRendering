/*
See the LICENSE.txt file for this sample’s licensing information.

Abstract:
The implementation of the iOS view controller.
*/

#import "AAPLViewController.h"
#import "AAPLRenderer.h"
#import <MetalKit/MetalKit.h>

@implementation AAPLViewController
{
    MTKView *_view;

    AAPLRenderer *_renderer;
    UISegmentedControl *_renderModeControl;
    UISlider *_exposureSlider;
    UISwitch *_denoiseSwitch;
}

- (void)viewDidLoad
{
    [super viewDidLoad];

    [_loadingSpinner startAnimating];
    [self _setupRenderControls];
    
    _view = (MTKView *)self.view;
    _view.layer.backgroundColor = [UIColor colorWithRed:0.65 green:0.65 blue:0.65 alpha:1.0].CGColor;
    _view.device = MTLCreateSystemDefaultDevice();
    _view.preferredFramesPerSecond = 120;

    if (!_view.device)
    {
        NSLog(@"Metal is not supported on this device.");
        [_loadingSpinner stopAnimating];
        _loadingLabel.text = @"Metal is unavailable on this device.";
        return;
    }

    dispatch_queue_t q = dispatch_get_global_queue( QOS_CLASS_USER_INITIATED, 0 );
    
    CGSize size = _view.bounds.size;
    __weak AAPLViewController* weakSelf = self;
    dispatch_async( q, ^(){
        AAPLViewController* strongSelf;
        if ( (strongSelf = weakSelf) )
        {
            strongSelf->_renderer = [[AAPLRenderer alloc] initWithMetalKitView:strongSelf->_view size:size];

            if (!strongSelf->_renderer)
            {
                dispatch_async(dispatch_get_main_queue(), ^(){
                    AAPLViewController* innerStrongSelf;
                    if ( (innerStrongSelf = weakSelf) )
                    {
                        [innerStrongSelf->_loadingSpinner stopAnimating];
                        innerStrongSelf->_loadingLabel.text = @"Renderer failed initialization.";
                    }
                });
                return;
            }

            dispatch_async( dispatch_get_main_queue(), ^(){
                AAPLViewController* innerStrongSelf;
                if ( (innerStrongSelf = weakSelf) )
                {
                    [innerStrongSelf->_loadingSpinner stopAnimating];
                    innerStrongSelf->_loadingLabel.hidden = YES;

                    [innerStrongSelf->_renderer mtkView:innerStrongSelf->_view
                                 drawableSizeWillChange:innerStrongSelf->_view.drawableSize];

                    innerStrongSelf->_view.delegate = innerStrongSelf->_renderer;
                    [innerStrongSelf _applyInitialControlValues];
                }
            });
        }
    } );

}

- (void)_setupRenderControls
{
    UIView *backdrop = [[UIView alloc] init];
    backdrop.translatesAutoresizingMaskIntoConstraints = NO;
    backdrop.backgroundColor = [UIColor colorWithWhite:0.0 alpha:0.45];
    backdrop.layer.cornerRadius = 12.0;

    UILabel *renderModeLabel = [[UILabel alloc] init];
    renderModeLabel.translatesAutoresizingMaskIntoConstraints = NO;
    renderModeLabel.text = @"Render Mode";
    renderModeLabel.textColor = UIColor.whiteColor;
    renderModeLabel.font = [UIFont preferredFontForTextStyle:UIFontTextStyleCaption1];

    _renderModeControl = [[UISegmentedControl alloc] initWithItems:@[@"Raster", @"Hybrid", @"Reflections"]];
    _renderModeControl.translatesAutoresizingMaskIntoConstraints = NO;
    _renderModeControl.selectedSegmentIndex = RMMetalRaytracing;
    [_renderModeControl addTarget:self action:@selector(_onRenderModeChanged:) forControlEvents:UIControlEventValueChanged];

    UILabel *exposureLabel = [[UILabel alloc] init];
    exposureLabel.translatesAutoresizingMaskIntoConstraints = NO;
    exposureLabel.text = @"Exposure";
    exposureLabel.textColor = UIColor.whiteColor;
    exposureLabel.font = [UIFont preferredFontForTextStyle:UIFontTextStyleCaption1];

    _exposureSlider = [[UISlider alloc] init];
    _exposureSlider.translatesAutoresizingMaskIntoConstraints = NO;
    _exposureSlider.minimumValue = 0.0f;
    _exposureSlider.maximumValue = 2.0f;
    _exposureSlider.value = 1.0f;
    [_exposureSlider addTarget:self action:@selector(_onExposureChanged:) forControlEvents:UIControlEventValueChanged];

    UILabel *denoiseLabel = [[UILabel alloc] init];
    denoiseLabel.translatesAutoresizingMaskIntoConstraints = NO;
    denoiseLabel.text = @"Denoise";
    denoiseLabel.textColor = UIColor.whiteColor;
    denoiseLabel.font = [UIFont preferredFontForTextStyle:UIFontTextStyleCaption1];

    _denoiseSwitch = [[UISwitch alloc] init];
    _denoiseSwitch.translatesAutoresizingMaskIntoConstraints = NO;
    _denoiseSwitch.on = YES;
    [_denoiseSwitch addTarget:self action:@selector(_onDenoiseChanged:) forControlEvents:UIControlEventValueChanged];

    [backdrop addSubview:renderModeLabel];
    [backdrop addSubview:_renderModeControl];
    [backdrop addSubview:exposureLabel];
    [backdrop addSubview:_exposureSlider];
    [backdrop addSubview:denoiseLabel];
    [backdrop addSubview:_denoiseSwitch];
    [self.view addSubview:backdrop];

    UILayoutGuide *safeArea = self.view.safeAreaLayoutGuide;
    [NSLayoutConstraint activateConstraints:@[
        [backdrop.leadingAnchor constraintEqualToAnchor:safeArea.leadingAnchor constant:12.0],
        [backdrop.trailingAnchor constraintEqualToAnchor:safeArea.trailingAnchor constant:-12.0],
        [backdrop.bottomAnchor constraintEqualToAnchor:safeArea.bottomAnchor constant:-12.0],

        [renderModeLabel.topAnchor constraintEqualToAnchor:backdrop.topAnchor constant:10.0],
        [renderModeLabel.leadingAnchor constraintEqualToAnchor:backdrop.leadingAnchor constant:12.0],

        [_renderModeControl.topAnchor constraintEqualToAnchor:renderModeLabel.bottomAnchor constant:6.0],
        [_renderModeControl.leadingAnchor constraintEqualToAnchor:backdrop.leadingAnchor constant:12.0],
        [_renderModeControl.trailingAnchor constraintEqualToAnchor:backdrop.trailingAnchor constant:-12.0],

        [exposureLabel.topAnchor constraintEqualToAnchor:_renderModeControl.bottomAnchor constant:10.0],
        [exposureLabel.leadingAnchor constraintEqualToAnchor:backdrop.leadingAnchor constant:12.0],

        [_exposureSlider.topAnchor constraintEqualToAnchor:exposureLabel.bottomAnchor constant:6.0],
        [_exposureSlider.leadingAnchor constraintEqualToAnchor:backdrop.leadingAnchor constant:12.0],
        [_exposureSlider.trailingAnchor constraintEqualToAnchor:backdrop.trailingAnchor constant:-12.0],

        [denoiseLabel.topAnchor constraintEqualToAnchor:_exposureSlider.bottomAnchor constant:10.0],
        [denoiseLabel.leadingAnchor constraintEqualToAnchor:backdrop.leadingAnchor constant:12.0],
        [denoiseLabel.bottomAnchor constraintEqualToAnchor:backdrop.bottomAnchor constant:-10.0],

        [_denoiseSwitch.centerYAnchor constraintEqualToAnchor:denoiseLabel.centerYAnchor],
        [_denoiseSwitch.trailingAnchor constraintEqualToAnchor:backdrop.trailingAnchor constant:-12.0]
    ]];
}

- (void)_applyInitialControlValues
{
    [_renderer setRenderMode:(RenderMode)_renderModeControl.selectedSegmentIndex];
    [_renderer setExposure:_exposureSlider.value];
    [_renderer setDenoisingEnabled:_denoiseSwitch.on];
}

- (void)_onRenderModeChanged:(UISegmentedControl *)sender
{
    if (_renderer)
    {
        [_renderer setRenderMode:(RenderMode)sender.selectedSegmentIndex];
    }
}

- (void)_onExposureChanged:(UISlider *)sender
{
    if (_renderer)
    {
        [_renderer setExposure:sender.value];
    }
}

- (void)_onDenoiseChanged:(UISwitch *)sender
{
    if (_renderer)
    {
        [_renderer setDenoisingEnabled:sender.on];
    }
}

@end
