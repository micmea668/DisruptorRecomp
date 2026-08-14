#ifndef DISRUPTOR_MOUSE_AIM_H
#define DISRUPTOR_MOUSE_AIM_H

/*
 * Live, host-side control settings for Disruptor.  These functions are safe
 * to call from the runtime thread after SDL has been initialised.  Boolean
 * values use 0/1 so the interface remains consumable from both C and C++.
 */

#ifdef __cplusplus
extern "C" {
#endif

int  disruptor_mouse_aim_enabled(void);
void disruptor_mouse_aim_set_enabled(int enabled);

int  disruptor_modern_controls_enabled(void);
void disruptor_modern_controls_set_enabled(int enabled);

int  disruptor_high_precision_camera_enabled(void);
void disruptor_high_precision_camera_set_enabled(int enabled);

double disruptor_mouse_horizontal_sensitivity(void);
/* Returns the effective sensitivity after clamping to the supported range.
 * A non-finite value is rejected and leaves the current value unchanged. */
double disruptor_mouse_set_horizontal_sensitivity(double sensitivity);

int  disruptor_mouse_invert_horizontal(void);
void disruptor_mouse_set_invert_horizontal(int inverted);

/* Disruptor has no native pitch state.  Vertical look therefore owns a
 * separate signed host pitch, expressed in the same 256-units-per-turn
 * angular domain as the retail yaw.  The camera/aim extension consumes the
 * requested value and independently decides whether the current presentation
 * and gameplay state can apply it safely. */
int  disruptor_mouse_vertical_look_enabled(void);
void disruptor_mouse_set_vertical_look_enabled(int enabled);

double disruptor_mouse_vertical_sensitivity(void);
/* Uses the same finite 0.005..2.000 range as horizontal sensitivity. */
double disruptor_mouse_set_vertical_sensitivity(double sensitivity);

int  disruptor_mouse_invert_vertical(void);
void disruptor_mouse_set_invert_vertical(int inverted);

/* Current requested pitch in signed 256-units-per-turn units. */
double disruptor_mouse_vertical_pitch(void);
void disruptor_mouse_recenter_vertical(void);

int disruptor_mouse_captured(void);
/* Returns 1 when the requested state is active.  Capture can only be enabled
 * while either mouse axis or modern controls are enabled; release is always
 * allowed. */
int disruptor_mouse_set_captured(int captured);

#ifdef __cplusplus
}  /* extern "C" */
#endif

#endif  /* DISRUPTOR_MOUSE_AIM_H */
