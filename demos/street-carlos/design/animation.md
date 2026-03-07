# Animations

Animations will use melody's animation system. The usecase is simple enough to not need something completely custom. 
This means that if something is not setup the way this game needs, it's melody's fault and it should change.

```c
Mel_Anim_Clip_Spritesheet_Handle clip = mel_anim_spritesheet_clip_create();
Mel_Anim_Frame_Handle frame1 = mel_anim_spritesheet_clip_add_frame(clip);
mel_anim_spritesheet_clip_frame_set_index(frame1, 1);
mel_anim_spritesheet_clip_frame_set_duration(frame1, 0.10f);
```
