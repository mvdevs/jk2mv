#include "tr_local.h"
#include "../qcommon/cm_public.h"


/*
=================
R_CullTriSurf

Returns true if the grid is completely culled away.
Also sets the clipped hint bit in tess
=================
*/
static qboolean	R_CullTriSurf( srfTriangles_t *cv ) {
	int	boxCull;

	boxCull = R_CullLocalBox( cv->bounds );

	if ( boxCull == CULL_OUT ) {
		return qtrue;
	}
	return qfalse;
}

/*
=================
R_CullGrid

Returns true if the grid is completely culled away.
Also sets the clipped hint bit in tess
=================
*/
static qboolean	R_CullGrid( srfGridMesh_t *cv ) {
	int	boxCull;
	int	sphereCull;

	if ( r_nocurves->integer ) {
		return qtrue;
	}

	if ( tr.currentEntityNum != ENTITYNUM_WORLD ) {
		sphereCull = R_CullLocalPointAndRadius( cv->localOrigin, cv->meshRadius );
	} else {
		sphereCull = R_CullPointAndRadius( cv->localOrigin, cv->meshRadius );
	}
	boxCull = CULL_OUT;

	// check for trivial reject
	if ( sphereCull == CULL_OUT )
	{
		tr.pc.c_sphere_cull_patch_out++;
		return qtrue;
	}
	// check bounding box if necessary
	else if ( sphereCull == CULL_CLIP )
	{
		tr.pc.c_sphere_cull_patch_clip++;

		boxCull = R_CullLocalBox( cv->meshBounds );

		if ( boxCull == CULL_OUT )
		{
			tr.pc.c_box_cull_patch_out++;
			return qtrue;
		}
		else if ( boxCull == CULL_IN )
		{
			tr.pc.c_box_cull_patch_in++;
		}
		else
		{
			tr.pc.c_box_cull_patch_clip++;
		}
	}
	else
	{
		tr.pc.c_sphere_cull_patch_in++;
	}

	return qfalse;
}


/*
================
R_CullSurface

Tries to back face cull surfaces before they are lighted or
added to the sorting list.

This will also allow mirrors on both sides of a model without recursion.
================
*/
static qboolean	R_CullSurface( surfaceType_t *surface, shader_t *shader ) {
	srfSurfaceFace_t *sface;
	float			d;

	if ( r_nocull->integer ) {
		return qfalse;
	}

	if ( *surface == SF_GRID ) {
		return R_CullGrid( (srfGridMesh_t *)surface );
	}

	if ( *surface == SF_TRIANGLES ) {
		return R_CullTriSurf( (srfTriangles_t *)surface );
	}

	if ( *surface != SF_FACE ) {
		return qfalse;
	}

	if ( shader->cullType == CT_TWO_SIDED ) {
		return qfalse;
	}

	// face culling
	if ( !r_facePlaneCull->integer ) {
		return qfalse;
	}

	sface = ( srfSurfaceFace_t * ) surface;
	d = DotProduct (tr.ori.viewOrigin, sface->plane.normal);

	// don't cull exactly on the plane, because there are levels of rounding
	// through the BSP, ICD, and hardware that may cause pixel gaps if an
	// epsilon isn't allowed here
	if ( shader->cullType == CT_FRONT_SIDED ) {
		if ( d < sface->plane.dist - 8 ) {
			return qtrue;
		}
	} else {
		if ( d > sface->plane.dist + 8 ) {
			return qtrue;
		}
	}

	return qfalse;
}


static int R_DlightFace( srfSurfaceFace_t *face, int dlightBits ) {
	float		d;
	int			i;
	dlight_t	*dl;

	for ( i = 0 ; i < tr.refdef.num_dlights ; i++ ) {
		if ( ! ( dlightBits & ( 1 << i ) ) ) {
			continue;
		}
		dl = &tr.refdef.dlights[i];
		d = DotProduct( dl->origin, face->plane.normal ) - face->plane.dist;
		if ( d < -dl->radius || d > dl->radius ) {
			// dlight doesn't reach the plane
			dlightBits &= ~( 1 << i );
		}
	}

	if ( !dlightBits ) {
		tr.pc.c_dlightSurfacesCulled++;
	}

	face->dlightBits[ tr.smpFrame ] = dlightBits;
	return dlightBits;
}

static int R_DlightGrid( srfGridMesh_t *grid, int dlightBits ) {
	int			i;
	dlight_t	*dl;

	for ( i = 0 ; i < tr.refdef.num_dlights ; i++ ) {
		if ( ! ( dlightBits & ( 1 << i ) ) ) {
			continue;
		}
		dl = &tr.refdef.dlights[i];
		if ( dl->origin[0] - dl->radius > grid->meshBounds[1][0]
			|| dl->origin[0] + dl->radius < grid->meshBounds[0][0]
			|| dl->origin[1] - dl->radius > grid->meshBounds[1][1]
			|| dl->origin[1] + dl->radius < grid->meshBounds[0][1]
			|| dl->origin[2] - dl->radius > grid->meshBounds[1][2]
			|| dl->origin[2] + dl->radius < grid->meshBounds[0][2] ) {
			// dlight doesn't reach the bounds
			dlightBits &= ~( 1 << i );
		}
	}

	if ( !dlightBits ) {
		tr.pc.c_dlightSurfacesCulled++;
	}

	grid->dlightBits[ tr.smpFrame ] = dlightBits;
	return dlightBits;
}


static int R_DlightTrisurf( srfTriangles_t *surf, int dlightBits ) {
	// FIXME: more dlight culling to trisurfs...
	surf->dlightBits[ tr.smpFrame ] = dlightBits;
	return dlightBits;
#if 0
	int			i;
	dlight_t	*dl;

	for ( i = 0 ; i < tr.refdef.num_dlights ; i++ ) {
		if ( ! ( dlightBits & ( 1 << i ) ) ) {
			continue;
		}
		dl = &tr.refdef.dlights[i];
		if ( dl->origin[0] - dl->radius > grid->meshBounds[1][0]
			|| dl->origin[0] + dl->radius < grid->meshBounds[0][0]
			|| dl->origin[1] - dl->radius > grid->meshBounds[1][1]
			|| dl->origin[1] + dl->radius < grid->meshBounds[0][1]
			|| dl->origin[2] - dl->radius > grid->meshBounds[1][2]
			|| dl->origin[2] + dl->radius < grid->meshBounds[0][2] ) {
			// dlight doesn't reach the bounds
			dlightBits &= ~( 1 << i );
		}
	}

	if ( !dlightBits ) {
		tr.pc.c_dlightSurfacesCulled++;
	}

	grid->dlightBits[ tr.smpFrame ] = dlightBits;
	return dlightBits;
#endif
}

/*
====================
R_DlightSurface

The given surface is going to be drawn, and it touches a leaf
that is touched by one or more dlights, so try to throw out
more dlights if possible.
====================
*/
static int R_DlightSurface( msurface_t *surf, int dlightBits ) {
	if ( *surf->data == SF_FACE ) {
		dlightBits = R_DlightFace( (srfSurfaceFace_t *)surf->data, dlightBits );
	} else if ( *surf->data == SF_GRID ) {
		dlightBits = R_DlightGrid( (srfGridMesh_t *)surf->data, dlightBits );
	} else if ( *surf->data == SF_TRIANGLES ) {
		dlightBits = R_DlightTrisurf( (srfTriangles_t *)surf->data, dlightBits );
	} else {
		dlightBits = 0;
	}

	if ( dlightBits ) {
		tr.pc.c_dlightSurfaces++;
	}

	return dlightBits;
}



/*
======================
R_AddWorldSurface
======================
*/
static void R_AddWorldSurface( msurface_t *surf, int dlightBits ) {
	if ( surf->viewCount == tr.viewCount ) {
		return;		// already in this view
	}

	surf->viewCount = tr.viewCount;
	// FIXME: bmodel fog?

	// try to cull before dlighting or adding
	if ( R_CullSurface( surf->data, surf->shader ) ) {
		return;
	}

	// check for dlighting
	if ( dlightBits ) {
		dlightBits = R_DlightSurface( surf, dlightBits );
		dlightBits = ( dlightBits != 0 );
	}

	R_AddDrawSurf( surf->data, surf->shader, surf->fogIndex, dlightBits );
}

/*
=============================================================

	BRUSH MODELS

=============================================================
*/

/*
=================
R_AddBrushModelSurfaces
=================
*/
void R_AddBrushModelSurfaces ( trRefEntity_t *ent ) {
	bmodel_t	*bmodel;
	int			clip;
	model_t		*pModel;
	int			i;

	pModel = R_GetModelByHandle( ent->e.hModel );

	bmodel = pModel->bmodel;

	clip = R_CullLocalBox( bmodel->bounds );
	if ( clip == CULL_OUT ) {
		return;
	}

	R_DlightBmodel( bmodel );

	for ( i = 0 ; i < bmodel->numSurfaces ; i++ ) {
		R_AddWorldSurface( bmodel->firstSurface + i, tr.currentEntity->needDlights );
	}
}

static float GetQuadArea( const vec3_t v[4], const vec3_t normal )
{
	// v are quad corners in clockwise order. When they don't form
	// convex quad, area will be negative.
	vec3_t	vec1, vec2, dis1, dis2, area;

	// area of v0, v1, v3 tri (may be negative when v[0] is concave)
	VectorSubtract( v[3], v[0], vec1 );
	VectorSubtract( v[1], v[0], vec2 );
	CrossProduct( vec1, vec2, dis1 );

	// area of v1, v2, v3 tri (may be negative when v[2] is concave)
	VectorSubtract( v[1], v[2], vec1 );
	VectorSubtract( v[3], v[2], vec2 );
	CrossProduct( vec1, vec2, dis2 );

	// dis1, dis2 are colinear because quad points are on a single
	// plane
	VectorAdd(dis1, dis2, area);

	return DotProduct(normal, area);
}

void RE_GetBModelVerts( int bmodelIndex, vec3_t *verts, vec3_t normal )
{
	bmodel_t			*bmodel;
	model_t				*pModel;

	pModel = R_GetModelByHandle( bmodelIndex );
	bmodel = pModel->bmodel;

	if (!bmodel)
	{
		ri.Error(ERR_DROP, "RE_GetBModelVerts: non-brush model");
	}

	if (bmodel->numSurfaces == 0)
	{
		ri.Error(ERR_DROP, "RE_GetBModelVerts: model has no surfaces");
	}

	// Group surfaces laying on the same plane
	int *surfaceGrp = (int *)ri.Hunk_AllocateTempMemory(bmodel->numSurfaces * sizeof(int));

	for (int i = 0; i < bmodel->numSurfaces; i++)
		surfaceGrp[i] = -1;

	int grpNum = 0;
	for (int i = 0; i < bmodel->numSurfaces; i++)
	{
		if (surfaceGrp[i] != -1)
			continue;

		surfaceGrp[i] = grpNum;

		msurface_t *surf = bmodel->firstSurface + i;
		srfSurfaceFace_t *face = (srfSurfaceFace_t *)surf->data;

		for (int j = i + 1; j < bmodel->numSurfaces; j++)
		{
			if (surfaceGrp[j] != -1)
				continue;

			msurface_t *jsurf = bmodel->firstSurface + j;
			srfSurfaceFace_t *jface = (srfSurfaceFace_t *)jsurf->data;

			// that's a pretty big epsilon but 0.01f was not enough for duel_training
			if (fabsf(face->plane.dist - jface->plane.dist) < 0.1f &&
				DistanceSquared(face->plane.normal, jface->plane.normal) < 0.001f * 0.001f)
			{
				surfaceGrp[j] = grpNum;
			}
		}

		grpNum++;
	}

	// Calculate total camera-facing area of each surface group
	float *grpScore = (float *)ri.Hunk_AllocateTempMemory(grpNum * sizeof(float));

	for (int i = 0; i < grpNum; i++)
		grpScore[i] = 0.0f;

	for (int i = 0; i < bmodel->numSurfaces; i++)
	{
		msurface_t *surf = bmodel->firstSurface + i;
		srfSurfaceFace_t *face = (srfSurfaceFace_t *)surf->data;
		unsigned *indices = (unsigned *)(((char *)face) + face->ofsIndices );
		int numTris = face->numIndices / 3;
		float faceArea = 0.0f;

		// Calculate area of face
		for (int j = 0; j < numTris; j++) {
			float *v1 = face->points[indices[3 * j + 0]];
			float *v2 = face->points[indices[3 * j + 1]];
			float *v3 = face->points[indices[3 * j + 2]];
			vec3_t vec1, vec2, cross;

			// Get area of tri (times 2)
			VectorSubtract(v1, v2, vec1);
			VectorSubtract(v1, v3, vec2);
			CrossProduct(vec1, vec2, cross);
			faceArea += VectorLength(cross);
		}

		// Score gives precedence to surfaces oriented towards camera,
		// but when there is a big difference in area size (area)
		// orientation becomes irrelevant and bigger surface will be
		// selected.
		grpScore[surfaceGrp[i]] += faceArea * (1.0f - 0.1f * DotProduct(face->plane.normal, tr.refdef.viewaxis[0]));
	}

	int bestGrp = 0;
	for (int i = 1; i < grpNum; i++)
	{
		if (grpScore[i] > grpScore[bestGrp])
			bestGrp = i;
	}

	ri.Hunk_FreeTempMemory(grpScore);

	// Now find best quad surface representing the group. The
	// algorithm makes assumption that combined surfaces within a
	// group (on the same plane) are roughly the shape of a single
	// convex quad. No holes or unconnected parts allowed.
	vec3_t qverts[4];
	float quadArea;
	qboolean seeded = qfalse;

	for (int i = 0; i < bmodel->numSurfaces; i++)
	{
		if (surfaceGrp[i] != bestGrp)
			continue;

		msurface_t *surf = bmodel->firstSurface + i;
		srfSurfaceFace_t *face = (srfSurfaceFace_t *)surf->data;

		// seed with empty quad
		if (!seeded) {
			seeded = qtrue;
			VectorCopy(face->points[0], qverts[0]);
			VectorCopy(face->points[1], qverts[1]);
			VectorCopy(face->points[2], qverts[2]);
			VectorCopy(face->points[2], qverts[3]); // degenerate
			quadArea = GetQuadArea(qverts, face->plane.normal);
			if (quadArea < 0.0f) {
				// order qverts so that quadArea is positive
				VectorCopy(face->points[2], qverts[1]);
				VectorCopy(face->points[1], qverts[2]);
				quadArea = - quadArea;
			}
		}

		for (int j = 0; j < face->numPoints; j++) {
			// Try to replace each corner in quad with surface point
			// and see if quad area increased. For arbitrary convex
			// surface result would depend heavily on initial seeding
			// point, but under assumption that surface resembles quad
			// result should be the same or very similar regardless
			// where we start and cover the quad.
			int bestCorner = -1;
			int bestArea = quadArea;
			for (int k = 0; k < 4; k++) {
				vec3_t temp;
				float area;
				VectorCopy(qverts[k], temp);
				VectorCopy(face->points[j], qverts[k]);
				area = GetQuadArea(qverts, face->plane.normal);
				if (area > bestArea) {
					bestCorner = k;
					bestArea = area;
				}
				VectorCopy(temp, qverts[k]);
			}

			if (bestCorner != -1) {
				quadArea = bestArea;
				VectorCopy(face->points[j], qverts[bestCorner]);
			}
		}
	}

	for (int i = 0; i < 4; i++) {
		VectorCopy(qverts[i], verts[i]);
	}

	ri.Hunk_FreeTempMemory(surfaceGrp);
}

/*
=============================================================

	WORLD MODEL

=============================================================
*/


/*
================
R_RecursiveWorldNode
================
*/
static void R_RecursiveWorldNode( mnode_t *node, int planeBits, unsigned int dlightBits ) {

	do {
		unsigned int	newDlights[2];

		// if the node wasn't marked as potentially visible, exit
		if (node->visframe != tr.visCount) {
			return;
		}

		// if the bounding volume is outside the frustum, nothing
		// inside can be visible OPTIMIZE: don't do this all the way to leafs?

		if ( !r_nocull->integer ) {
			int		r;

			if ( planeBits & 1 ) {
				r = BoxOnPlaneSide(node->mins, node->maxs, &tr.viewParms.frustum[0]);
				if (r == 2) {
					return;						// culled
				}
				if ( r == 1 ) {
					planeBits &= ~1;			// all descendants will also be in front
				}
			}

			if ( planeBits & 2 ) {
				r = BoxOnPlaneSide(node->mins, node->maxs, &tr.viewParms.frustum[1]);
				if (r == 2) {
					return;						// culled
				}
				if ( r == 1 ) {
					planeBits &= ~2;			// all descendants will also be in front
				}
			}

			if ( planeBits & 4 ) {
				r = BoxOnPlaneSide(node->mins, node->maxs, &tr.viewParms.frustum[2]);
				if (r == 2) {
					return;						// culled
				}
				if ( r == 1 ) {
					planeBits &= ~4;			// all descendants will also be in front
				}
			}

			if ( planeBits & 8 ) {
				r = BoxOnPlaneSide(node->mins, node->maxs, &tr.viewParms.frustum[3]);
				if (r == 2) {
					return;						// culled
				}
				if ( r == 1 ) {
					planeBits &= ~8;			// all descendants will also be in front
				}
			}

		}

		if ( node->contents != -1 ) {
			break;
		}

		// node is just a decision point, so go down both sides
		// since we don't care about sort orders, just go positive to negative

		// determine which dlights are needed
		newDlights[0] = 0;
		newDlights[1] = 0;
		if ( dlightBits ) {
			int	i;

			for ( i = 0 ; i < tr.refdef.num_dlights ; i++ ) {
				dlight_t	*dl;
				float		dist;

				if ( dlightBits & ( 1 << i ) ) {
					dl = &tr.refdef.dlights[i];
					dist = DotProduct( dl->origin, node->plane->normal ) - node->plane->dist;

					if ( dist > -dl->radius ) {
						newDlights[0] |= ( 1 << i );
					}
					if ( dist < dl->radius ) {
						newDlights[1] |= ( 1 << i );
					}
				}
			}
		}

		// recurse down the children, front side first
		R_RecursiveWorldNode (node->children[0], planeBits, newDlights[0] );

		// tail recurse
		node = node->children[1];
		dlightBits = newDlights[1];
	} while ( 1 );

	{
		// leaf node, so add mark surfaces
		int			c;
		msurface_t	*surf, **mark;

		tr.pc.c_leafs++;

		// add to z buffer bounds
		if ( node->mins[0] < tr.viewParms.visBounds[0][0] ) {
			tr.viewParms.visBounds[0][0] = node->mins[0];
		}
		if ( node->mins[1] < tr.viewParms.visBounds[0][1] ) {
			tr.viewParms.visBounds[0][1] = node->mins[1];
		}
		if ( node->mins[2] < tr.viewParms.visBounds[0][2] ) {
			tr.viewParms.visBounds[0][2] = node->mins[2];
		}

		if ( node->maxs[0] > tr.viewParms.visBounds[1][0] ) {
			tr.viewParms.visBounds[1][0] = node->maxs[0];
		}
		if ( node->maxs[1] > tr.viewParms.visBounds[1][1] ) {
			tr.viewParms.visBounds[1][1] = node->maxs[1];
		}
		if ( node->maxs[2] > tr.viewParms.visBounds[1][2] ) {
			tr.viewParms.visBounds[1][2] = node->maxs[2];
		}

		// add the individual surfaces
		mark = node->firstmarksurface;
		c = node->nummarksurfaces;
		while (c--) {
			// the surface may have already been added if it
			// spans multiple leafs
			surf = *mark;
			R_AddWorldSurface( surf, dlightBits );
			mark++;
		}
	}

}


/*
===============
R_PointInLeaf
===============
*/
static mnode_t *R_PointInLeaf( const vec3_t p ) {
	mnode_t		*node;
	float		d;
	cplane_t	*plane;

	if ( !tr.world ) {
		ri.Error (ERR_DROP, "R_PointInLeaf: bad model");
	}

	node = tr.world->nodes;
	while( 1 ) {
		if (node->contents != -1) {
			break;
		}
		plane = node->plane;
		d = DotProduct (p,plane->normal) - plane->dist;
		if (d > 0) {
			node = node->children[0];
		} else {
			node = node->children[1];
		}
	}

	return node;
}

/*
==============
R_ClusterPVS
==============
*/
static const byte *R_ClusterPVS (int cluster) {
	if (!tr.world || !tr.world->vis || cluster < 0 || cluster >= tr.world->numClusters ) {
		return tr.world->novis;
	}

	return tr.world->vis + cluster * tr.world->clusterBytes;
}

/*
=================
R_inPVS
=================
*/
qboolean R_inPVS( const vec3_t p1, const vec3_t p2 ) {
	mnode_t *leaf;
	byte	*vis;

	leaf = R_PointInLeaf( p1 );
	vis = CM_ClusterPVS( leaf->cluster );
	leaf = R_PointInLeaf( p2 );

	if ( !(vis[leaf->cluster>>3] & (1<<(leaf->cluster&7))) ) {
		return qfalse;
	}
	return qtrue;
}

/*
===============
R_MarkLeaves

Mark the leaves and nodes that are in the PVS for the current
cluster
===============
*/
static void R_MarkLeaves (void) {
	const byte	*vis;
	mnode_t	*leaf, *parent;
	int		i;
	int		cluster;

	// lockpvs lets designers walk around to determine the
	// extent of the current pvs
	if ( r_lockpvs->integer ) {
		return;
	}

	// current viewcluster
	leaf = R_PointInLeaf( tr.viewParms.pvsOrigin );
	cluster = leaf->cluster;

	// if the cluster is the same and the area visibility matrix
	// hasn't changed, we don't need to mark everything again

	// if r_showcluster was just turned on, remark everything
	if ( tr.viewCluster == cluster && !tr.refdef.areamaskModified
		&& !r_showcluster->modified ) {
		return;
	}

	if ( r_showcluster->modified || r_showcluster->integer ) {
		r_showcluster->modified = qfalse;
		if ( r_showcluster->integer ) {
			ri.Printf( PRINT_ALL, "cluster:%i  area:%i\n", cluster, leaf->area );
		}
	}

	tr.visCount++;
	tr.viewCluster = cluster;

	if ( r_novis->integer || tr.viewCluster == -1 ) {
		for (i=0 ; i<tr.world->numnodes ; i++) {
			if (tr.world->nodes[i].contents != CONTENTS_SOLID) {
				tr.world->nodes[i].visframe = tr.visCount;
			}
		}
		return;
	}

	vis = R_ClusterPVS (tr.viewCluster);

	for (i=0,leaf=tr.world->nodes ; i<tr.world->numnodes ; i++, leaf++) {
		cluster = leaf->cluster;
		if ( cluster < 0 || cluster >= tr.world->numClusters ) {
			continue;
		}

		// check general pvs
		if ( !(vis[cluster>>3] & (1<<(cluster&7))) ) {
			continue;
		}

		// check for door connection
		if ( leaf->area >= 8 * (int)ARRAY_LEN(tr.refdef.areamask) ||
			 (tr.refdef.areamask[leaf->area>>3] & (1<<(leaf->area&7)) ) ) {
			continue;		// not visible
		}

		parent = leaf;
		do {
			if (parent->visframe == tr.visCount)
				break;
			parent->visframe = tr.visCount;
			parent = parent->parent;
		} while (parent);
	}
}


/*
=============
R_AddWorldSurfaces
=============
*/
void R_AddWorldSurfaces (void) {
	unsigned int dlightBits;

	if ( !r_drawworld->integer ) {
		return;
	}

	if ( tr.refdef.rdflags & RDF_NOWORLDMODEL ) {
		return;
	}

	tr.currentEntityNum = ENTITYNUM_WORLD;
	tr.shiftedEntityNum = tr.currentEntityNum << QSORT_ENTITYNUM_SHIFT;

	// determine which leaves are in the PVS / areamask
	R_MarkLeaves ();

	// clear out the visible min/max
	ClearBounds( tr.viewParms.visBounds[0], tr.viewParms.visBounds[1] );

	// perform frustum culling and add all the potentially visible surfaces
	if ( tr.refdef.num_dlights > MAX_DLIGHTS ) {
		tr.refdef.num_dlights = MAX_DLIGHTS;
		dlightBits = -1;
	} else {
		dlightBits = ( 1U << tr.refdef.num_dlights ) - 1;
	}

	R_RecursiveWorldNode( tr.world->nodes, 15, dlightBits );
}
