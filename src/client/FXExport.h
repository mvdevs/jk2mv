#ifndef FX_EXPORT_H_INC
#define FX_EXPORT_H_INC

int	FX_RegisterEffect(const char *file);

void FX_PlaySimpleEffect( const char *file, const vec3_t org );					// uses a default up axis
void FX_PlayEffect( const char *file, const vec3_t org, const vec3_t fwd );		// builds arbitrary perp. right vector, does a cross product to define up
void FX_PlayEntityEffect( const char *file, const vec3_t org,
						const vec3_t axis[3], const int boltInfo, const int entNum );

void FX_PlaySimpleEffectID( int id, const vec3_t org );					// uses a default up axis
void FX_PlayEffectID( int id, const vec3_t org, const vec3_t fwd );		// builds arbitrary perp. right vector, does a cross product to define up
void FX_PlayEntityEffectID( int id, const vec3_t org,
						const vec3_t axis[3], const int boltInfo, const int entNum );
void FX_PlayBoltedEffectID( int id, sharedBoltInterface_t *fxObj );

void FX_AddScheduledEffects( void );

int			FX_InitSystem( void );	// called in CG_Init to purge the fx system.
qboolean	FX_FreeSystem( void );	// ditches all active effects;
void		FX_AdjustTime_Pos( int time, const vec3_t refdef_vieworg, const vec3_t refdef_viewaxis[3] );


#endif // FX_EXPORT_H_INC
