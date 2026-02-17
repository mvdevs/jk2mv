#include "tr_local.h"
#include "vk_local.h"


/*

  for a projection shadow:

  point[x] += light vector * ( z - shadow plane )
  point[y] +=
  point[z] = shadow plane

  1 0 light[x] / light[z]

*/

typedef struct {
	int		i2;
	int		facing;
} edgeDef_t;

#define	MAX_EDGE_DEFS	32

static	edgeDef_t	edgeDefs[SHADER_MAX_VERTEXES][MAX_EDGE_DEFS];
static	int			numEdgeDefs[SHADER_MAX_VERTEXES];
static	int			facing[SHADER_MAX_INDEXES/3];

void R_AddEdgeDef( int i1, int i2, int facing ) {
	int		c;

	c = numEdgeDefs[ i1 ];
	if ( c == MAX_EDGE_DEFS ) {
		return;		// overflow
	}
	edgeDefs[ i1 ][ c ].i2 = i2;
	edgeDefs[ i1 ][ c ].facing = facing;

	numEdgeDefs[ i1 ]++;
}

void R_RenderShadowEdges( void ) {
	int		i;

	int		c, c2;
	int		j, k;
	int		i2;
	int		c_edges, c_rejected;
	int		hit[2];

	c_edges = 0;
	c_rejected = 0;

	for ( i = 0 ; i < tess.numVertexes ; i++ ) {
		c = numEdgeDefs[ i ];
		for ( j = 0 ; j < c ; j++ ) {
			if ( !edgeDefs[ i ][ j ].facing ) {
				continue;
			}

			hit[0] = 0;
			hit[1] = 0;

			i2 = edgeDefs[ i ][ j ].i2;
			c2 = numEdgeDefs[ i2 ];
			for ( k = 0 ; k < c2 ; k++ ) {
				if ( edgeDefs[ i2 ][ k ].i2 == i ) {
					hit[ edgeDefs[ i2 ][ k ].facing ]++;
				}
			}

			// if it doesn't share the edge with another front facing
			// triangle, it is a sil edge
			if ( hit[ 1 ] == 0 ) {
				// Vulkan: draw shadow edge quad as two triangles
				vec4_t positions[4];
				glIndex_t indexes[6] = { 0, 1, 2, 0, 2, 3 };
				byte colors[4][4];

				VectorCopy( tess.xyz[ i ], positions[0] ); positions[0][3] = 1.0f;
				VectorCopy( tess.xyz[ i + tess.numVertexes ], positions[1] ); positions[1][3] = 1.0f;
				VectorCopy( tess.xyz[ i2 ], positions[2] ); positions[2][3] = 1.0f;
				VectorCopy( tess.xyz[ i2 + tess.numVertexes ], positions[3] ); positions[3][3] = 1.0f;

				for (int v = 0; v < 4; v++) {
					colors[v][0] = 51; colors[v][1] = 51; colors[v][2] = 51; colors[v][3] = 255;
				}

				VK_DrawIndexed( 4, (float*)positions, NULL, NULL, (byte*)colors, 6, indexes );

				c_edges++;
			} else {
				c_rejected++;
			}
		}
	}
}

/*
=================
RB_ShadowTessEnd

triangleFromEdge[ v1 ][ v2 ]


  set triangle from edge( v1, v2, tri )
  if ( facing[ triangleFromEdge[ v1 ][ v2 ] ] && !facing[ triangleFromEdge[ v2 ][ v1 ] ) {
  }
=================
*/
void RB_ShadowTessEnd( void ) {
	int		i;
	int		numTris;
	vec3_t	lightDir;

	// we can only do this if we have enough space in the vertex buffers
	if ( tess.numVertexes >= SHADER_MAX_VERTEXES / 2 ) {
		return;
	}

	if ( glConfig.stencilBits < 4 ) {
		return;
	}

	VectorCopy( backEnd.currentEntity->lightDir, lightDir );

	// project vertexes away from light direction
	for ( i = 0 ; i < tess.numVertexes ; i++ ) {
		VectorMA( tess.xyz[i], -512, lightDir, tess.xyz[i+tess.numVertexes] );
	}

	// decide which triangles face the light
	Com_Memset( numEdgeDefs, 0, 4 * tess.numVertexes );

	numTris = tess.numIndexes / 3;
	for ( i = 0 ; i < numTris ; i++ ) {
		int		i1, i2, i3;
		vec3_t	d1, d2, normal;
		float	*v1, *v2, *v3;
		float	d;

		i1 = tess.indexes[ i*3 + 0 ];
		i2 = tess.indexes[ i*3 + 1 ];
		i3 = tess.indexes[ i*3 + 2 ];

		v1 = tess.xyz[ i1 ];
		v2 = tess.xyz[ i2 ];
		v3 = tess.xyz[ i3 ];

		VectorSubtract( v2, v1, d1 );
		VectorSubtract( v3, v1, d2 );
		CrossProduct( d1, d2, normal );

		d = DotProduct( normal, lightDir );
		if ( d > 0 ) {
			facing[ i ] = 1;
		} else {
			facing[ i ] = 0;
		}

		// create the edges
		R_AddEdgeDef( i1, i2, facing[ i ] );
		R_AddEdgeDef( i2, i3, facing[ i ] );
		R_AddEdgeDef( i3, i1, facing[ i ] );
	}

	// draw the silhouette edges

	R_BindImage( tr.whiteImage );
	R_SetCullMode( CT_FRONT_SIDED );
	R_SetStateBits( GLS_SRCBLEND_ONE | GLS_DSTBLEND_ZERO );

	// Push safe fragment defaults to prevent stale alphaTest from discarding shadow edges
	{
		struct {
			float texEnvMode;
			float alphaTestFunc;
			float alphaTestValue;
		} fragDefaults = { 0.0f, 0.0f, 0.0f };  // MODULATE, no alpha test
		VkCommandBuffer cmd2 = vk.frames[vk.currentFrame].commandBuffer;
		vkCmdPushConstants( cmd2, vk.pipelineLayout,
			VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
			80, sizeof(fragDefaults), &fragDefaults );
	}

	// Vulkan: bind stencil increment pipeline (front faces, incr stencil on depth pass)
	if ( vk.shadowStencilIncrPipeline ) {
		VkCommandBuffer cmd = vk.frames[vk.currentFrame].commandBuffer;
		vkCmdBindPipeline( cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, vk.shadowStencilIncrPipeline );
	} else {
		return; // no shadow pipeline available
	}

	R_RenderShadowEdges();

	// Vulkan: bind stencil decrement pipeline (back faces, decr stencil on depth pass)
	R_SetCullMode( CT_BACK_SIDED );
	if ( vk.shadowStencilDecrPipeline ) {
		VkCommandBuffer cmd = vk.frames[vk.currentFrame].commandBuffer;
		vkCmdBindPipeline( cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, vk.shadowStencilDecrPipeline );
	} else {
		return;
	}

	R_RenderShadowEdges();
}


/*
=================
RB_ShadowFinish

Darken everything that is is a shadow volume.
We have to delay this until everything has been shadowed,
because otherwise shadows from different body parts would
overlap and double darken.
=================
*/
void RB_ShadowFinish( void ) {
	if ( r_shadows->integer != 2 ) {
		return;
	}
	if ( glConfig.stencilBits < 4 ) {
		return;
	}

	// Vulkan: bind stencil-tested shadow finish pipeline (darkens where stencil != 0)
	R_BindImage( tr.whiteImage );
	R_SetStateBits( GLS_DEPTHMASK_TRUE | GLS_SRCBLEND_DST_COLOR | GLS_DSTBLEND_ZERO );

	VK_Set2D();

	if ( vk.shadowFinishPipeline ) {
		VkCommandBuffer cmd = vk.frames[vk.currentFrame].commandBuffer;
		vkCmdBindPipeline( cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, vk.shadowFinishPipeline );

		// Push safe fragment defaults to prevent stale alphaTest from discarding
		struct {
			float texEnvMode;
			float alphaTestFunc;
			float alphaTestValue;
		} fragDefaults = { 0.0f, 0.0f, 0.0f };
		vkCmdPushConstants( cmd, vk.pipelineLayout,
			VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
			80, sizeof(fragDefaults), &fragDefaults );
	} else {
		return; // no shadow finish pipeline available
	}

	// draw fullscreen darkening quad

	vec4_t positions[4] = {
		{ -100, 100, -10, 1 },
		{ 100, 100, -10, 1 },
		{ 100, -100, -10, 1 },
		{ -100, -100, -10, 1 }
	};
	glIndex_t indexes[6] = { 0, 1, 2, 0, 2, 3 };
	byte colors[4][4];
	for (int i = 0; i < 4; i++) {
		colors[i][0] = 153; colors[i][1] = 153; colors[i][2] = 153; colors[i][3] = 255;
	}

	VK_DrawIndexed( 4, (float*)positions, NULL, NULL, (byte*)colors, 6, indexes );
}


/*
=================
RB_ProjectionShadowDeform

=================
*/
void RB_ProjectionShadowDeform( void ) {
	float	*xyz;
	int		i;
	float	h;
	vec3_t	ground;
	vec3_t	light;
	float	groundDist;
	float	d;
	vec3_t	lightDir;

	xyz = ( float * ) tess.xyz;

	ground[0] = backEnd.ori.axis[0][2];
	ground[1] = backEnd.ori.axis[1][2];
	ground[2] = backEnd.ori.axis[2][2];

	groundDist = backEnd.ori.origin[2] - backEnd.currentEntity->e.shadowPlane;

	VectorCopy( backEnd.currentEntity->lightDir, lightDir );
	d = DotProduct( lightDir, ground );
	// don't let the shadows get too long or go negative
	if ( d < 0.5f ) {
		VectorMA( lightDir, (0.5f - d), ground, lightDir );
		d = DotProduct( lightDir, ground );
	}
	d = 1.0f / d;

	light[0] = lightDir[0] * d;
	light[1] = lightDir[1] * d;
	light[2] = lightDir[2] * d;

	for ( i = 0; i < tess.numVertexes; i++, xyz += 4 ) {
		h = DotProduct( xyz, ground ) + groundDist;

		xyz[0] -= light[0] * h;
		xyz[1] -= light[1] * h;
		xyz[2] -= light[2] * h;
	}
}
