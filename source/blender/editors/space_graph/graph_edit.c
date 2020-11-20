/*
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 *
 * The Original Code is Copyright (C) 2001-2002 by NaN Holding BV.
 * All rights reserved.
 */

/** \file
 * \ingroup spgraph
 */

#include <float.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#ifdef WITH_AUDASPACE
#  include <AUD_Special.h>
#endif

#include "MEM_guardedalloc.h"

#include "BLI_blenlib.h"
#include "BLI_math.h"
#include "BLI_utildefines.h"

#include "DNA_anim_types.h"
#include "DNA_scene_types.h"

#include "RNA_access.h"
#include "RNA_define.h"
#include "RNA_enum_types.h"

#include "BLT_translation.h"

#include "BKE_animsys.h"
#include "BKE_context.h"
#include "BKE_fcurve.h"
#include "BKE_global.h"
#include "BKE_nla.h"
#include "BKE_report.h"

#include "DEG_depsgraph_build.h"

#include "UI_interface.h"
#include "UI_view2d.h"

#include "ED_anim_api.h"
#include "ED_keyframes_edit.h"
#include "ED_keyframing.h"
#include "ED_markers.h"
#include "ED_numinput.h"
#include "ED_screen.h"
#include "ED_transform.h"

#include "WM_api.h"
#include "WM_types.h"

#include "graph_intern.h"

/* ************************************************************************** */
/* KEYFRAME-RANGE STUFF */

/* *************************** Calculate Range ************************** */

/* Get the min/max keyframes. */
/* Note: it should return total boundbox, filter for selection only can be argument... */
void get_graph_keyframe_extents(bAnimContext *ac,
                                float *xmin,
                                float *xmax,
                                float *ymin,
                                float *ymax,
                                const bool do_sel_only,
                                const bool include_handles)
{
  Scene *scene = ac->scene;
  SpaceGraph *sipo = (SpaceGraph *)ac->sl;

  ListBase anim_data = {NULL, NULL};
  bAnimListElem *ale;
  int filter;

  /* Get data to filter, from Dopesheet. */
  filter = (ANIMFILTER_DATA_VISIBLE | ANIMFILTER_CURVE_VISIBLE | ANIMFILTER_NODUPLIS);
  if (sipo->flag & SIPO_SELCUVERTSONLY) {
    filter |= ANIMFILTER_SEL;
  }

  ANIM_animdata_filter(ac, &anim_data, filter, ac->data, ac->datatype);

  /* Set large values initial values that will be easy to override. */
  if (xmin) {
    *xmin = 999999999.0f;
  }
  if (xmax) {
    *xmax = -999999999.0f;
  }
  if (ymin) {
    *ymin = 999999999.0f;
  }
  if (ymax) {
    *ymax = -999999999.0f;
  }

  /* Check if any channels to set range with. */
  if (anim_data.first) {
    bool foundBounds = false;

    /* Go through channels, finding max extents. */
    for (ale = anim_data.first; ale; ale = ale->next) {
      AnimData *adt = ANIM_nla_mapping_get(ac, ale);
      FCurve *fcu = (FCurve *)ale->key_data;
      float txmin, txmax, tymin, tymax;
      float unitFac, offset;

      /* Get range. */
      if (BKE_fcurve_calc_bounds(
              fcu, &txmin, &txmax, &tymin, &tymax, do_sel_only, include_handles)) {
        short mapping_flag = ANIM_get_normalization_flags(ac);

        /* Apply NLA scaling. */
        if (adt) {
          txmin = BKE_nla_tweakedit_remap(adt, txmin, NLATIME_CONVERT_MAP);
          txmax = BKE_nla_tweakedit_remap(adt, txmax, NLATIME_CONVERT_MAP);
        }

        /* Apply unit corrections. */
        unitFac = ANIM_unit_mapping_get_factor(ac->scene, ale->id, fcu, mapping_flag, &offset);
        tymin += offset;
        tymax += offset;
        tymin *= unitFac;
        tymax *= unitFac;

        /* Try to set cur using these values, if they're more extreme than previously set values.
         */
        if ((xmin) && (txmin < *xmin)) {
          *xmin = txmin;
        }
        if ((xmax) && (txmax > *xmax)) {
          *xmax = txmax;
        }
        if ((ymin) && (tymin < *ymin)) {
          *ymin = tymin;
        }
        if ((ymax) && (tymax > *ymax)) {
          *ymax = tymax;
        }

        foundBounds = true;
      }
    }

    /* Ensure that the extents are not too extreme that view implodes...*/
    if (foundBounds) {
      if ((xmin && xmax) && (fabsf(*xmax - *xmin) < 0.001f)) {
        *xmin -= 0.0005f;
        *xmax += 0.0005f;
      }
      if ((ymin && ymax) && (fabsf(*ymax - *ymin) < 0.001f)) {
        *ymax -= 0.0005f;
        *ymax += 0.0005f;
      }
    }
    else {
      if (xmin) {
        *xmin = (float)PSFRA;
      }
      if (xmax) {
        *xmax = (float)PEFRA;
      }
      if (ymin) {
        *ymin = -5;
      }
      if (ymax) {
        *ymax = 5;
      }
    }

    /* Free memory. */
    ANIM_animdata_freelist(&anim_data);
  }
  else {
    /* Set default range. */
    if (ac->scene) {
      if (xmin) {
        *xmin = (float)PSFRA;
      }
      if (xmax) {
        *xmax = (float)PEFRA;
      }
    }
    else {
      if (xmin) {
        *xmin = -5;
      }
      if (xmax) {
        *xmax = 100;
      }
    }

    if (ymin) {
      *ymin = -5;
    }
    if (ymax) {
      *ymax = 5;
    }
  }
}

/* ****************** Automatic Preview-Range Operator ****************** */

static int graphkeys_previewrange_exec(bContext *C, wmOperator *UNUSED(op))
{
  bAnimContext ac;
  Scene *scene;
  float min, max;

  /* Get editor data. */
  if (ANIM_animdata_get_context(C, &ac) == 0) {
    return OPERATOR_CANCELLED;
  }
  if (ac.scene == NULL) {
    return OPERATOR_CANCELLED;
  }

  scene = ac.scene;

  /* Set the range directly. */
  get_graph_keyframe_extents(&ac, &min, &max, NULL, NULL, false, false);
  scene->r.flag |= SCER_PRV_RANGE;
  scene->r.psfra = round_fl_to_int(min);
  scene->r.pefra = round_fl_to_int(max);

  /* Set notifier that things have changed. */
  // XXX Err... there's nothing for frame ranges yet, but this should do fine too.
  WM_event_add_notifier(C, NC_SCENE | ND_FRAME, ac.scene);

  return OPERATOR_FINISHED;
}

void GRAPH_OT_previewrange_set(wmOperatorType *ot)
{
  /* Identifiers */
  ot->name = "Auto-Set Preview Range";
  ot->idname = "GRAPH_OT_previewrange_set";
  ot->description = "Automatically set Preview Range based on range of keyframes";

  /* API callbacks */
  ot->exec = graphkeys_previewrange_exec;
  /* XXX: unchecked poll to get fsamples working too, but makes modifier damage trickier. */
  ot->poll = ED_operator_graphedit_active;

  /* Flags */
  ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;
}

/* ****************** View-All Operator ****************** */

static int graphkeys_viewall(bContext *C,
                             const bool do_sel_only,
                             const bool include_handles,
                             const int smooth_viewtx)
{
  bAnimContext ac;
  rctf cur_new;

  /* Get editor data. */
  if (ANIM_animdata_get_context(C, &ac) == 0) {
    return OPERATOR_CANCELLED;
  }

  /* Set the horizontal range, with an extra offset so that the extreme keys will be in view. */
  get_graph_keyframe_extents(&ac,
                             &cur_new.xmin,
                             &cur_new.xmax,
                             &cur_new.ymin,
                             &cur_new.ymax,
                             do_sel_only,
                             include_handles);

  /* Give some more space at the borders. */
  BLI_rctf_scale(&cur_new, 1.1f);

  /* Take regions into account, that could block the view.
   * Marker region is supposed to be larger than the scroll-bar, so prioritize it.*/
  float pad_top = UI_TIME_SCRUB_MARGIN_Y;
  float pad_bottom = BLI_listbase_is_empty(ED_context_get_markers(C)) ? V2D_SCROLL_HANDLE_HEIGHT :
                                                                        UI_MARKER_MARGIN_Y;
  BLI_rctf_pad_y(&cur_new, ac.region->winy, pad_bottom, pad_top);

  UI_view2d_smooth_view(C, ac.region, &cur_new, smooth_viewtx);
  return OPERATOR_FINISHED;
}

/* ......... */

static int graphkeys_viewall_exec(bContext *C, wmOperator *op)
{
  const bool include_handles = RNA_boolean_get(op->ptr, "include_handles");
  const int smooth_viewtx = WM_operator_smooth_viewtx_get(op);

  /* Whole range */
  return graphkeys_viewall(C, false, include_handles, smooth_viewtx);
}

static int graphkeys_view_selected_exec(bContext *C, wmOperator *op)
{
  const bool include_handles = RNA_boolean_get(op->ptr, "include_handles");
  const int smooth_viewtx = WM_operator_smooth_viewtx_get(op);

  /* Only selected. */
  return graphkeys_viewall(C, true, include_handles, smooth_viewtx);
}

/* ......... */

void GRAPH_OT_view_all(wmOperatorType *ot)
{
  /* Identifiers */
  ot->name = "Frame All";
  ot->idname = "GRAPH_OT_view_all";
  ot->description = "Reset viewable area to show full keyframe range";

  /* API callbacks */
  ot->exec = graphkeys_viewall_exec;
  /* XXX: Unchecked poll to get fsamples working too, but makes modifier damage trickier... */
  ot->poll = ED_operator_graphedit_active;

  /* Flags */
  ot->flag = 0;

  /* Props */
  ot->prop = RNA_def_boolean(ot->srna,
                             "include_handles",
                             true,
                             "Include Handles",
                             "Include handles of keyframes when calculating extents");
}

void GRAPH_OT_view_selected(wmOperatorType *ot)
{
  /* Identifiers */
  ot->name = "Frame Selected";
  ot->idname = "GRAPH_OT_view_selected";
  ot->description = "Reset viewable area to show selected keyframe range";

  /* API callbacks */
  ot->exec = graphkeys_view_selected_exec;
  /* XXX: Unchecked poll to get fsamples working too, but makes modifier damage trickier... */
  ot->poll = ED_operator_graphedit_active;

  /* Flags */
  ot->flag = 0;

  /* Props */
  ot->prop = RNA_def_boolean(ot->srna,
                             "include_handles",
                             true,
                             "Include Handles",
                             "Include handles of keyframes when calculating extents");
}

/* ********************** View Frame Operator ****************************** */

static int graphkeys_view_frame_exec(bContext *C, wmOperator *op)
{
  const int smooth_viewtx = WM_operator_smooth_viewtx_get(op);
  ANIM_center_frame(C, smooth_viewtx);
  return OPERATOR_FINISHED;
}

void GRAPH_OT_view_frame(wmOperatorType *ot)
{
  /* Identifiers */
  ot->name = "Go to Current Frame";
  ot->idname = "GRAPH_OT_view_frame";
  ot->description = "Move the view to the current frame";

  /* API callbacks */
  ot->exec = graphkeys_view_frame_exec;
  ot->poll = ED_operator_graphedit_active;

  /* Flags */
  ot->flag = 0;
}

/* ******************** Create Ghost-Curves Operator *********************** */
/* This operator samples the data of the selected F-Curves to F-Points, storing them
 * as 'ghost curves' in the active Graph Editor.
 */

/* Bake each F-Curve into a set of samples, and store as a ghost curve. */
static void create_ghost_curves(bAnimContext *ac, int start, int end)
{
  SpaceGraph *sipo = (SpaceGraph *)ac->sl;
  ListBase anim_data = {NULL, NULL};
  bAnimListElem *ale;
  int filter;

  /* Free existing ghost curves. */
  BKE_fcurves_free(&sipo->runtime.ghost_curves);

  /* Sanity check. */
  if (start >= end) {
    printf("Error: Frame range for Ghost F-Curve creation is inappropriate\n");
    return;
  }

  /* Filter data. */
  filter = (ANIMFILTER_DATA_VISIBLE | ANIMFILTER_CURVE_VISIBLE | ANIMFILTER_SEL |
            ANIMFILTER_NODUPLIS);
  ANIM_animdata_filter(ac, &anim_data, filter, ac->data, ac->datatype);

  /* Loop through filtered data and add keys between selected keyframes on every frame . */
  for (ale = anim_data.first; ale; ale = ale->next) {
    FCurve *fcu = (FCurve *)ale->key_data;
    FCurve *gcu = BKE_fcurve_create();
    AnimData *adt = ANIM_nla_mapping_get(ac, ale);
    ChannelDriver *driver = fcu->driver;
    FPoint *fpt;
    float unitFac, offset;
    int cfra;
    short mapping_flag = ANIM_get_normalization_flags(ac);

    /* Disable driver so that it don't muck up the sampling process. */
    fcu->driver = NULL;

    /* Calculate unit-mapping factor. */
    unitFac = ANIM_unit_mapping_get_factor(ac->scene, ale->id, fcu, mapping_flag, &offset);

    /* Create samples, but store them in a new curve
     * - we cannot use fcurve_store_samples() as that will only overwrite the original curve.
     */
    gcu->fpt = fpt = MEM_callocN(sizeof(FPoint) * (end - start + 1), "Ghost FPoint Samples");
    gcu->totvert = end - start + 1;

    /* Use the sampling callback at 1-frame intervals from start to end frames. */
    for (cfra = start; cfra <= end; cfra++, fpt++) {
      float cfrae = BKE_nla_tweakedit_remap(adt, cfra, NLATIME_CONVERT_UNMAP);

      fpt->vec[0] = cfrae;
      fpt->vec[1] = (fcurve_samplingcb_evalcurve(fcu, NULL, cfrae) + offset) * unitFac;
    }

    /* Set color of ghost curve
     * - make the color slightly darker.
     */
    gcu->color[0] = fcu->color[0] - 0.07f;
    gcu->color[1] = fcu->color[1] - 0.07f;
    gcu->color[2] = fcu->color[2] - 0.07f;

    /* Store new ghost curve. */
    BLI_addtail(&sipo->runtime.ghost_curves, gcu);

    /* Restore driver. */
    fcu->driver = driver;
  }

  /* Admin and redraws. */
  ANIM_animdata_freelist(&anim_data);
}

/* ------------------- */

static int graphkeys_create_ghostcurves_exec(bContext *C, wmOperator *UNUSED(op))
{
  bAnimContext ac;
  View2D *v2d;
  int start, end;

  /* Get editor data. */
  if (ANIM_animdata_get_context(C, &ac) == 0) {
    return OPERATOR_CANCELLED;
  }

  /* Ghost curves are snapshots of the visible portions of the curves,
   * so set range to be the visible range. */
  v2d = &ac.region->v2d;
  start = (int)v2d->cur.xmin;
  end = (int)v2d->cur.xmax;

  /* Bake selected curves into a ghost curve. */
  create_ghost_curves(&ac, start, end);

  /* Update this editor only. */
  ED_area_tag_redraw(CTX_wm_area(C));

  return OPERATOR_FINISHED;
}

void GRAPH_OT_ghost_curves_create(wmOperatorType *ot)
{
  /* Identifiers */
  ot->name = "Create Ghost Curves";
  ot->idname = "GRAPH_OT_ghost_curves_create";
  ot->description =
      "Create snapshot (Ghosts) of selected F-Curves as background aid for active Graph Editor";

  /* API callbacks */
  ot->exec = graphkeys_create_ghostcurves_exec;
  ot->poll = graphop_visible_keyframes_poll;

  /* Flags */
  ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;

  /* TODO: add props for start/end frames */
}

/* ******************** Clear Ghost-Curves Operator *********************** */
/* This operator clears the 'ghost curves' for the active Graph Editor */

static int graphkeys_clear_ghostcurves_exec(bContext *C, wmOperator *UNUSED(op))
{
  bAnimContext ac;
  SpaceGraph *sipo;

  /* Get editor data. */
  if (ANIM_animdata_get_context(C, &ac) == 0) {
    return OPERATOR_CANCELLED;
  }
  sipo = (SpaceGraph *)ac.sl;

  /* If no ghost curves, don't do anything. */
  if (BLI_listbase_is_empty(&sipo->runtime.ghost_curves)) {
    return OPERATOR_CANCELLED;
  }
  /* Free ghost curves. */
  BKE_fcurves_free(&sipo->runtime.ghost_curves);

  /* Update this editor only. */
  ED_area_tag_redraw(CTX_wm_area(C));

  return OPERATOR_FINISHED;
}

void GRAPH_OT_ghost_curves_clear(wmOperatorType *ot)
{
  /* Identifiers */
  ot->name = "Clear Ghost Curves";
  ot->idname = "GRAPH_OT_ghost_curves_clear";
  ot->description = "Clear F-Curve snapshots (Ghosts) for active Graph Editor";

  /* API callbacks */
  ot->exec = graphkeys_clear_ghostcurves_exec;
  ot->poll = ED_operator_graphedit_active;

  /* Flags */
  ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;
}

/* ************************************************************************** */
/* GENERAL STUFF */

/* ******************** Insert Keyframes Operator ************************* */

/* Mode defines for insert keyframes tool. */
typedef enum eGraphKeys_InsertKey_Types {
  GRAPHKEYS_INSERTKEY_ALL = (1 << 0),
  GRAPHKEYS_INSERTKEY_SEL = (1 << 1),
  GRAPHKEYS_INSERTKEY_CURSOR = (1 << 2),
  GRAPHKEYS_INSERTKEY_ACTIVE = (1 << 3),
} eGraphKeys_InsertKey_Types;

/* RNA mode types for insert keyframes tool. */
static const EnumPropertyItem prop_graphkeys_insertkey_types[] = {
    {GRAPHKEYS_INSERTKEY_ALL,
     "ALL",
     0,
     "All Channels",
     "Insert a keyframe on all visible and editable F-Curves using each curve's current value"},
    {GRAPHKEYS_INSERTKEY_SEL,
     "SEL",
     0,
     "Only Selected Channels",
     "Insert a keyframe on selected F-Curves using each curve's current value"},
    {GRAPHKEYS_INSERTKEY_ACTIVE | GRAPHKEYS_INSERTKEY_CURSOR,
     "CURSOR_ACTIVE",
     0,
     "Active Channels At Cursor",
     "Insert a keyframe for the active F-Curve at the cursor point"},
    {GRAPHKEYS_INSERTKEY_SEL | GRAPHKEYS_INSERTKEY_CURSOR,
     "CURSOR_SEL",
     0,
     "Selected Channels At Cursor",
     "Insert a keyframe for selected F-Curves at the cursor point"},
    {0, NULL, 0, NULL, NULL},
};

/* This function is responsible for snapping keyframes to frame-times. */
static void insert_graph_keys(bAnimContext *ac, eGraphKeys_InsertKey_Types mode)
{
  ListBase anim_data = {NULL, NULL};
  ListBase nla_cache = {NULL, NULL};
  bAnimListElem *ale;
  int filter;
  size_t num_items;

  ReportList *reports = ac->reports;
  SpaceGraph *sipo = (SpaceGraph *)ac->sl;
  Scene *scene = ac->scene;
  ToolSettings *ts = scene->toolsettings;
  eInsertKeyFlags flag = 0;

  /* Filter data. */
  filter = (ANIMFILTER_DATA_VISIBLE | ANIMFILTER_CURVE_VISIBLE | ANIMFILTER_FOREDIT |
            ANIMFILTER_NODUPLIS);
  if (mode & GRAPHKEYS_INSERTKEY_SEL) {
    filter |= ANIMFILTER_SEL;
  }
  else if (mode & GRAPHKEYS_INSERTKEY_ACTIVE) {
    filter |= ANIMFILTER_ACTIVE;
  }

  num_items = ANIM_animdata_filter(ac, &anim_data, filter, ac->data, ac->datatype);
  if (num_items == 0) {
    if (mode & GRAPHKEYS_INSERTKEY_ACTIVE) {
      BKE_report(reports,
                 RPT_ERROR,
                 "No active F-Curve to add a keyframe to. Select an editable F-Curve first");
    }
    else if (mode & GRAPHKEYS_INSERTKEY_SEL) {
      BKE_report(reports, RPT_ERROR, "No selected F-Curves to add keyframes to");
    }
    else {
      BKE_report(reports, RPT_ERROR, "No channels to add keyframes to");
    }

    return;
  }

  /* Init key-framing flag. */
  flag = ANIM_get_keyframing_flags(scene, true);

  /* Insert keyframes. */
  if (mode & GRAPHKEYS_INSERTKEY_CURSOR) {
    for (ale = anim_data.first; ale; ale = ale->next) {
      AnimData *adt = ANIM_nla_mapping_get(ac, ale);
      FCurve *fcu = (FCurve *)ale->key_data;

      short mapping_flag = ANIM_get_normalization_flags(ac);
      float offset;
      float unit_scale = ANIM_unit_mapping_get_factor(
          ac->scene, ale->id, ale->key_data, mapping_flag, &offset);

      float x, y;

      /* perform time remapping for x-coordinate (if necessary) */
      if ((sipo) && (sipo->mode == SIPO_MODE_DRIVERS)) {
        x = sipo->cursorTime;
      }
      else if (adt) {
        x = BKE_nla_tweakedit_remap(adt, (float)CFRA, NLATIME_CONVERT_UNMAP);
      }
      else {
        x = (float)CFRA;
      }

      /* Normalise units of cursor's value. */
      if (sipo) {
        y = (sipo->cursorVal / unit_scale) - offset;
      }
      else {
        y = 0.0f;
      }

      /* Insert keyframe directly into the F-Curve. */
      insert_vert_fcurve(fcu, x, y, ts->keyframe_type, 0);

      ale->update |= ANIM_UPDATE_DEFAULT;
    }
  }
  else {
    const AnimationEvalContext anim_eval_context = BKE_animsys_eval_context_construct(
        ac->depsgraph, (float)CFRA);
    for (ale = anim_data.first; ale; ale = ale->next) {
      FCurve *fcu = (FCurve *)ale->key_data;

      /* Read value from property the F-Curve represents, or from the curve only?
       *
       * - ale->id != NULL:
       *   Typically, this means that we have enough info to try resolving the path.
       * - ale->owner != NULL:
       *   If this is set, then the path may not be resolvable from the ID alone,
       *   so it's easier for now to just read the F-Curve directly.
       *   (TODO: add the full-blown PointerRNA relative parsing case here... (Joshua Leung 2015))
       * - fcu->driver != NULL:
       *   If this is set, then it's a driver. If we don't check for this, we'd end
       *   up adding the keyframes on a new F-Curve in the action data instead.
       */
      if (ale->id && !ale->owner && !fcu->driver) {
        insert_keyframe(ac->bmain,
                        reports,
                        ale->id,
                        NULL,
                        ((fcu->grp) ? (fcu->grp->name) : (NULL)),
                        fcu->rna_path,
                        fcu->array_index,
                        &anim_eval_context,
                        ts->keyframe_type,
                        &nla_cache,
                        flag);
      }
      else {
        AnimData *adt = ANIM_nla_mapping_get(ac, ale);

        /* Adjust current frame for NLA-mapping. */
        float cfra = (float)CFRA;
        if ((sipo) && (sipo->mode == SIPO_MODE_DRIVERS)) {
          cfra = sipo->cursorTime;
        }
        else if (adt) {
          cfra = BKE_nla_tweakedit_remap(adt, (float)CFRA, NLATIME_CONVERT_UNMAP);
        }

        const float curval = evaluate_fcurve_only_curve(fcu, cfra);
        insert_vert_fcurve(fcu, cfra, curval, ts->keyframe_type, 0);
      }

      ale->update |= ANIM_UPDATE_DEFAULT;
    }
  }

  BKE_animsys_free_nla_keyframing_context_cache(&nla_cache);

  ANIM_animdata_update(ac, &anim_data);
  ANIM_animdata_freelist(&anim_data);
}

/* ------------------- */

static int graphkeys_insertkey_exec(bContext *C, wmOperator *op)
{
  bAnimContext ac;
  eGraphKeys_InsertKey_Types mode;

  /* Get editor data. */
  if (ANIM_animdata_get_context(C, &ac) == 0) {
    return OPERATOR_CANCELLED;
  }

  /* Which channels to affect?. */
  mode = RNA_enum_get(op->ptr, "type");

  /* Insert keyframes. */
  insert_graph_keys(&ac, mode);

  /* Set notifier that keyframes have changed. */
  WM_event_add_notifier(C, NC_ANIMATION | ND_KEYFRAME | NA_ADDED, NULL);

  return OPERATOR_FINISHED;
}

void GRAPH_OT_keyframe_insert(wmOperatorType *ot)
{
  /* Identifiers */
  ot->name = "Insert Keyframes";
  ot->idname = "GRAPH_OT_keyframe_insert";
  ot->description = "Insert keyframes for the specified channels";

  /* API callbacks */
  ot->invoke = WM_menu_invoke;
  ot->exec = graphkeys_insertkey_exec;
  ot->poll = graphop_editable_keyframes_poll;

  /* Flags */
  ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;

  /* Id-props */
  ot->prop = RNA_def_enum(ot->srna, "type", prop_graphkeys_insertkey_types, 0, "Type", "");
}

/* ******************** Click-Insert Keyframes Operator ************************* */

static int graphkeys_click_insert_exec(bContext *C, wmOperator *op)
{
  bAnimContext ac;
  bAnimListElem *ale;
  AnimData *adt;
  FCurve *fcu;
  float frame, val;

  /* Get animation context. */
  if (ANIM_animdata_get_context(C, &ac) == 0) {
    return OPERATOR_CANCELLED;
  }

  /* Get active F-Curve 'anim-list-element'. */
  ale = get_active_fcurve_channel(&ac);
  if (ELEM(NULL, ale, ale->data)) {
    if (ale) {
      MEM_freeN(ale);
    }
    return OPERATOR_CANCELLED;
  }
  fcu = ale->data;

  /* When there are F-Modifiers on the curve, only allow adding
   * keyframes if these will be visible after doing so...
   */
  if (BKE_fcurve_is_keyframable(fcu)) {
    ListBase anim_data;
    ToolSettings *ts = ac.scene->toolsettings;

    short mapping_flag = ANIM_get_normalization_flags(&ac);
    float scale, offset;

    /* Preserve selection? */
    if (RNA_boolean_get(op->ptr, "extend") == false) {
      /* Deselect all keyframes first,
       * so that we can immediately start manipulating the newly added one(s)
       * - only affect the keyframes themselves, as we don't want channels popping in and out. */
      deselect_graph_keys(&ac, false, SELECT_SUBTRACT, false);
    }

    /* Get frame and value from props. */
    frame = RNA_float_get(op->ptr, "frame");
    val = RNA_float_get(op->ptr, "value");

    /* Apply inverse NLA-mapping to frame to get correct time in un-scaled action. */
    adt = ANIM_nla_mapping_get(&ac, ale);
    frame = BKE_nla_tweakedit_remap(adt, frame, NLATIME_CONVERT_UNMAP);

    /* Apply inverse unit-mapping to value to get correct value for F-Curves. */
    scale = ANIM_unit_mapping_get_factor(
        ac.scene, ale->id, fcu, mapping_flag | ANIM_UNITCONV_RESTORE, &offset);

    val = val * scale - offset;

    /* Insert keyframe on the specified frame + value. */
    insert_vert_fcurve(fcu, frame, val, ts->keyframe_type, 0);

    ale->update |= ANIM_UPDATE_DEPS;

    BLI_listbase_clear(&anim_data);
    BLI_addtail(&anim_data, ale);

    ANIM_animdata_update(&ac, &anim_data);
  }
  else {
    /* Warn about why this can't happen. */
    if (fcu->fpt) {
      BKE_report(op->reports, RPT_ERROR, "Keyframes cannot be added to sampled F-Curves");
    }
    else if (fcu->flag & FCURVE_PROTECTED) {
      BKE_report(op->reports, RPT_ERROR, "Active F-Curve is not editable");
    }
    else {
      BKE_report(op->reports, RPT_ERROR, "Remove F-Modifiers from F-Curve to add keyframes");
    }
  }

  /* Free temp data. */
  MEM_freeN(ale);

  /* Set notifier that keyframes have changed. */
  WM_event_add_notifier(C, NC_ANIMATION | ND_KEYFRAME | NA_EDITED, NULL);

  /* Done */
  return OPERATOR_FINISHED;
}

static int graphkeys_click_insert_invoke(bContext *C, wmOperator *op, const wmEvent *event)
{
  bAnimContext ac;
  ARegion *region;
  View2D *v2d;
  int mval[2];
  float x, y;

  /* Get animation context. */
  if (ANIM_animdata_get_context(C, &ac) == 0) {
    return OPERATOR_CANCELLED;
  }

  /* Store mouse coordinates in View2D space, into the operator's properties. */
  region = ac.region;
  v2d = &region->v2d;

  mval[0] = (event->x - region->winrct.xmin);
  mval[1] = (event->y - region->winrct.ymin);

  UI_view2d_region_to_view(v2d, mval[0], mval[1], &x, &y);

  RNA_float_set(op->ptr, "frame", x);
  RNA_float_set(op->ptr, "value", y);

  /* Run exec now. */
  return graphkeys_click_insert_exec(C, op);
}

void GRAPH_OT_click_insert(wmOperatorType *ot)
{
  /* Identifiers */
  ot->name = "Click-Insert Keyframes";
  ot->idname = "GRAPH_OT_click_insert";
  ot->description = "Insert new keyframe at the cursor position for the active F-Curve";

  /* API callbacks */
  ot->invoke = graphkeys_click_insert_invoke;
  ot->exec = graphkeys_click_insert_exec;
  ot->poll = graphop_active_fcurve_poll;

  /* Flags */
  ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;

  /* Properties */
  RNA_def_float(ot->srna,
                "frame",
                1.0f,
                -FLT_MAX,
                FLT_MAX,
                "Frame Number",
                "Frame to insert keyframe on",
                0,
                100);
  RNA_def_float(
      ot->srna, "value", 1.0f, -FLT_MAX, FLT_MAX, "Value", "Value for keyframe on", 0, 100);

  RNA_def_boolean(ot->srna,
                  "extend",
                  false,
                  "Extend",
                  "Extend selection instead of deselecting everything first");
}

/* ******************** Copy/Paste Keyframes Operator ************************* */
/* NOTE: the backend code for this is shared with the dopesheet editor */

static short copy_graph_keys(bAnimContext *ac)
{
  ListBase anim_data = {NULL, NULL};
  int filter, ok = 0;

  /* Clear buffer first. */
  ANIM_fcurves_copybuf_free();

  /* Filter data
   * - First time we try to filter more strictly, allowing only selected channels
   *   to allow copying animation between channels.
   */
  filter = (ANIMFILTER_DATA_VISIBLE | ANIMFILTER_CURVE_VISIBLE | ANIMFILTER_NODUPLIS);

  if (ANIM_animdata_filter(ac, &anim_data, filter | ANIMFILTER_SEL, ac->data, ac->datatype) == 0) {
    ANIM_animdata_filter(ac, &anim_data, filter, ac->data, ac->datatype);
  }

  /* Copy keyframes. */
  ok = copy_animedit_keys(ac, &anim_data);

  /* Clean up. */
  ANIM_animdata_freelist(&anim_data);

  return ok;
}

static short paste_graph_keys(bAnimContext *ac,
                              const eKeyPasteOffset offset_mode,
                              const eKeyMergeMode merge_mode,
                              bool flip)
{
  ListBase anim_data = {NULL, NULL};
  int filter, ok = 0;

  /* Filter data
   * - First time we try to filter more strictly, allowing only selected channels
   *   to allow copying animation between channels
   * - Second time, we loosen things up if nothing was found the first time, allowing
   *   users to just paste keyframes back into the original curve again T31670.
   */
  filter = (ANIMFILTER_DATA_VISIBLE | ANIMFILTER_CURVE_VISIBLE | ANIMFILTER_FOREDIT |
            ANIMFILTER_NODUPLIS);

  if (ANIM_animdata_filter(ac, &anim_data, filter | ANIMFILTER_SEL, ac->data, ac->datatype) == 0) {
    ANIM_animdata_filter(ac, &anim_data, filter, ac->data, ac->datatype);
  }

  /* Paste keyframes. */
  ok = paste_animedit_keys(ac, &anim_data, offset_mode, merge_mode, flip);

  /* Clean up. */
  ANIM_animdata_freelist(&anim_data);

  return ok;
}

/* ------------------- */

static int graphkeys_copy_exec(bContext *C, wmOperator *op)
{
  bAnimContext ac;

  /* Get editor data. */
  if (ANIM_animdata_get_context(C, &ac) == 0) {
    return OPERATOR_CANCELLED;
  }

  /* Copy keyframes. */
  if (copy_graph_keys(&ac)) {
    BKE_report(op->reports, RPT_ERROR, "No keyframes copied to keyframes copy/paste buffer");
    return OPERATOR_CANCELLED;
  }

  /* Just return - no operator needed here (no changes). */
  return OPERATOR_FINISHED;
}

void GRAPH_OT_copy(wmOperatorType *ot)
{
  /* Identifiers */
  ot->name = "Copy Keyframes";
  ot->idname = "GRAPH_OT_copy";
  ot->description = "Copy selected keyframes to the copy/paste buffer";

  /* API callbacks */
  ot->exec = graphkeys_copy_exec;
  ot->poll = graphop_editable_keyframes_poll;

  /* Flags */
  ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;
}

static int graphkeys_paste_exec(bContext *C, wmOperator *op)
{
  bAnimContext ac;

  const eKeyPasteOffset offset_mode = RNA_enum_get(op->ptr, "offset");
  const eKeyMergeMode merge_mode = RNA_enum_get(op->ptr, "merge");
  const bool flipped = RNA_boolean_get(op->ptr, "flipped");

  /* Get editor data. */
  if (ANIM_animdata_get_context(C, &ac) == 0) {
    return OPERATOR_CANCELLED;
  }

  /* Ac.reports by default will be the global reports list, which won't show warnings. */
  ac.reports = op->reports;

  /* Paste keyframes - non-zero return means an error occurred while trying to paste. */
  if (paste_graph_keys(&ac, offset_mode, merge_mode, flipped)) {
    return OPERATOR_CANCELLED;
  }

  /* Set notifier that keyframes have changed. */
  WM_event_add_notifier(C, NC_ANIMATION | ND_KEYFRAME | NA_EDITED, NULL);

  return OPERATOR_FINISHED;
}

void GRAPH_OT_paste(wmOperatorType *ot)
{
  PropertyRNA *prop;

  /* Identifiers */
  ot->name = "Paste Keyframes";
  ot->idname = "GRAPH_OT_paste";
  ot->description =
      "Paste keyframes from copy/paste buffer for the selected channels, starting on the current "
      "frame";

  /* API callbacks */

  // ot->invoke = WM_operator_props_popup; /* better wait for graph redo panel */
  ot->exec = graphkeys_paste_exec;
  ot->poll = graphop_editable_keyframes_poll;

  /* Flags */
  ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;

  /* Props */
  RNA_def_enum(ot->srna,
               "offset",
               rna_enum_keyframe_paste_offset_items,
               KEYFRAME_PASTE_OFFSET_CFRA_START,
               "Offset",
               "Paste time offset of keys");
  RNA_def_enum(ot->srna,
               "merge",
               rna_enum_keyframe_paste_merge_items,
               KEYFRAME_PASTE_MERGE_MIX,
               "Type",
               "Method of merging pasted keys and existing");
  prop = RNA_def_boolean(
      ot->srna, "flipped", false, "Flipped", "Paste keyframes from mirrored bones if they exist");
  RNA_def_property_flag(prop, PROP_SKIP_SAVE);
}

/* ******************** Duplicate Keyframes Operator ************************* */

static void duplicate_graph_keys(bAnimContext *ac)
{
  ListBase anim_data = {NULL, NULL};
  bAnimListElem *ale;
  int filter;

  /* Filter data. */
  filter = (ANIMFILTER_DATA_VISIBLE | ANIMFILTER_CURVE_VISIBLE | ANIMFILTER_FOREDIT |
            ANIMFILTER_NODUPLIS);
  ANIM_animdata_filter(ac, &anim_data, filter, ac->data, ac->datatype);

  /* Loop through filtered data and delete selected keys. */
  for (ale = anim_data.first; ale; ale = ale->next) {
    duplicate_fcurve_keys((FCurve *)ale->key_data);

    ale->update |= ANIM_UPDATE_DEFAULT;
  }

  ANIM_animdata_update(ac, &anim_data);
  ANIM_animdata_freelist(&anim_data);
}

/* ------------------- */

static int graphkeys_duplicate_exec(bContext *C, wmOperator *UNUSED(op))
{
  bAnimContext ac;

  /* Get editor data. */
  if (ANIM_animdata_get_context(C, &ac) == 0) {
    return OPERATOR_CANCELLED;
  }

  /* Duplicate keyframes. */
  duplicate_graph_keys(&ac);

  /* Set notifier that keyframes have changed. */
  WM_event_add_notifier(C, NC_ANIMATION | ND_KEYFRAME | NA_ADDED, NULL);

  return OPERATOR_FINISHED;
}

void GRAPH_OT_duplicate(wmOperatorType *ot)
{
  /* Identifiers */
  ot->name = "Duplicate Keyframes";
  ot->idname = "GRAPH_OT_duplicate";
  ot->description = "Make a copy of all selected keyframes";

  /* API callbacks */
  ot->exec = graphkeys_duplicate_exec;
  ot->poll = graphop_editable_keyframes_poll;

  /* Flags */
  ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;

  /* To give to transform. */
  RNA_def_enum(ot->srna, "mode", rna_enum_transform_mode_types, TFM_TRANSLATION, "Mode", "");
}

/* ******************** Delete Keyframes Operator ************************* */

static bool delete_graph_keys(bAnimContext *ac)
{
  ListBase anim_data = {NULL, NULL};
  bAnimListElem *ale;
  int filter;
  bool changed_final = false;

  /* Filter data. */
  filter = (ANIMFILTER_DATA_VISIBLE | ANIMFILTER_CURVE_VISIBLE | ANIMFILTER_FOREDIT |
            ANIMFILTER_NODUPLIS);
  ANIM_animdata_filter(ac, &anim_data, filter, ac->data, ac->datatype);

  /* Loop through filtered data and delete selected keys. */
  for (ale = anim_data.first; ale; ale = ale->next) {
    FCurve *fcu = (FCurve *)ale->key_data;
    AnimData *adt = ale->adt;
    bool changed;

    /* Delete selected keyframes only. */
    changed = delete_fcurve_keys(fcu);

    if (changed) {
      ale->update |= ANIM_UPDATE_DEFAULT;
      changed_final = true;
    }

    /* Only delete curve too if it won't be doing anything anymore. */
    if (BKE_fcurve_is_empty(fcu)) {
      ANIM_fcurve_delete_from_animdata(ac, adt, fcu);
      ale->key_data = NULL;
    }
  }

  ANIM_animdata_update(ac, &anim_data);
  ANIM_animdata_freelist(&anim_data);

  return changed_final;
}

/* ------------------- */

static int graphkeys_delete_exec(bContext *C, wmOperator *UNUSED(op))
{
  bAnimContext ac;

  /* Get editor data. */
  if (ANIM_animdata_get_context(C, &ac) == 0) {
    return OPERATOR_CANCELLED;
  }

  /* Delete keyframes. */
  if (!delete_graph_keys(&ac)) {
    return OPERATOR_CANCELLED;
  }

  /* Set notifier that keyframes have changed. */
  WM_event_add_notifier(C, NC_ANIMATION | ND_KEYFRAME | NA_REMOVED, NULL);

  return OPERATOR_FINISHED;
}

void GRAPH_OT_delete(wmOperatorType *ot)
{
  /* Identifiers */
  ot->name = "Delete Keyframes";
  ot->idname = "GRAPH_OT_delete";
  ot->description = "Remove all selected keyframes";

  /* API callbacks */
  ot->invoke = WM_operator_confirm;
  ot->exec = graphkeys_delete_exec;
  ot->poll = graphop_editable_keyframes_poll;

  /* Flags */
  ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;
}

/* ******************** Clean Keyframes Operator ************************* */

static void clean_graph_keys(bAnimContext *ac, float thresh, bool clean_chan)
{
  ListBase anim_data = {NULL, NULL};
  bAnimListElem *ale;
  int filter;

  /* Filter data. */
  filter = (ANIMFILTER_DATA_VISIBLE | ANIMFILTER_CURVE_VISIBLE | ANIMFILTER_FOREDIT |
            ANIMFILTER_SEL | ANIMFILTER_NODUPLIS);
  ANIM_animdata_filter(ac, &anim_data, filter, ac->data, ac->datatype);

  /* Loop through filtered data and clean curves. */
  for (ale = anim_data.first; ale; ale = ale->next) {
    clean_fcurve(ac, ale, thresh, clean_chan);

    ale->update |= ANIM_UPDATE_DEFAULT;
  }

  ANIM_animdata_update(ac, &anim_data);
  ANIM_animdata_freelist(&anim_data);
}

/* ------------------- */

static int graphkeys_clean_exec(bContext *C, wmOperator *op)
{
  bAnimContext ac;
  float thresh;
  bool clean_chan;

  /* Get editor data. */
  if (ANIM_animdata_get_context(C, &ac) == 0) {
    return OPERATOR_CANCELLED;
  }

  /* Get cleaning threshold. */
  thresh = RNA_float_get(op->ptr, "threshold");
  clean_chan = RNA_boolean_get(op->ptr, "channels");
  /* Clean keyframes. */
  clean_graph_keys(&ac, thresh, clean_chan);

  /* Set notifier that keyframes have changed. */
  WM_event_add_notifier(C, NC_ANIMATION | ND_KEYFRAME | NA_EDITED, NULL);

  return OPERATOR_FINISHED;
}

void GRAPH_OT_clean(wmOperatorType *ot)
{
  /* Identifiers */
  ot->name = "Clean Keyframes";
  ot->idname = "GRAPH_OT_clean";
  ot->description = "Simplify F-Curves by removing closely spaced keyframes";

  /* API callbacks */
  // ot->invoke = ???; /* XXX we need that number popup for this! */
  ot->exec = graphkeys_clean_exec;
  ot->poll = graphop_editable_keyframes_poll;

  /* Flags */
  ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;

  /* Properties */
  ot->prop = RNA_def_float(
      ot->srna, "threshold", 0.001f, 0.0f, FLT_MAX, "Threshold", "", 0.0f, 1000.0f);
  RNA_def_boolean(ot->srna, "channels", false, "Channels", "");
}

/* ******************** Decimate Keyframes Operator ************************* */

static void decimate_graph_keys(bAnimContext *ac, float remove_ratio, float error_sq_max)
{
  ListBase anim_data = {NULL, NULL};
  bAnimListElem *ale;
  int filter;

  /* Filter data. */
  filter = (ANIMFILTER_DATA_VISIBLE | ANIMFILTER_CURVE_VISIBLE | ANIMFILTER_FOREDIT |
            ANIMFILTER_SEL | ANIMFILTER_NODUPLIS);
  ANIM_animdata_filter(ac, &anim_data, filter, ac->data, ac->datatype);

  /* Loop through filtered data and clean curves. */
  for (ale = anim_data.first; ale; ale = ale->next) {
    if (!decimate_fcurve(ale, remove_ratio, error_sq_max)) {
      /* The selection contains unsupported keyframe types! */
      WM_report(RPT_WARNING, "Decimate: Skipping non linear/bezier keyframes!");
    }

    ale->update |= ANIM_UPDATE_DEFAULT;
  }

  ANIM_animdata_update(ac, &anim_data);
  ANIM_animdata_freelist(&anim_data);
}

/* ------------------- */

/* This data type is only used for modal operation. */
typedef struct tDecimateGraphOp {
  bAnimContext ac;
  Scene *scene;
  ScrArea *area;
  ARegion *region;

  /** A 0-1 value for determining how much we should decimate. */
  PropertyRNA *percentage_prop;

  /** The original bezt curve data (used for restoring fcurves).*/
  ListBase bezt_arr_list;

  NumInput num;
} tDecimateGraphOp;

typedef struct tBeztCopyData {
  int tot_vert;
  BezTriple *bezt;
} tBeztCopyData;

typedef enum tDecimModes {
  DECIM_RATIO = 1,
  DECIM_ERROR,
} tDecimModes;

/* Overwrite the current bezts arrays with the original data. */
static void decimate_reset_bezts(tDecimateGraphOp *dgo)
{
  ListBase anim_data = {NULL, NULL};
  LinkData *link_bezt;
  bAnimListElem *ale;
  int filter;

  bAnimContext *ac = &dgo->ac;

  /* Filter data. */
  filter = (ANIMFILTER_DATA_VISIBLE | ANIMFILTER_CURVE_VISIBLE | ANIMFILTER_FOREDIT |
            ANIMFILTER_SEL | ANIMFILTER_NODUPLIS);
  ANIM_animdata_filter(ac, &anim_data, filter, ac->data, ac->datatype);

  /* Loop through filtered data and reset bezts. */
  for (ale = anim_data.first, link_bezt = dgo->bezt_arr_list.first; ale; ale = ale->next) {
    FCurve *fcu = (FCurve *)ale->key_data;

    if (fcu->bezt == NULL) {
      /* This curve is baked, skip it. */
      continue;
    }

    tBeztCopyData *data = link_bezt->data;

    const int arr_size = sizeof(BezTriple) * data->tot_vert;

    MEM_freeN(fcu->bezt);

    fcu->bezt = MEM_mallocN(arr_size, __func__);
    fcu->totvert = data->tot_vert;

    memcpy(fcu->bezt, data->bezt, arr_size);

    link_bezt = link_bezt->next;
  }

  ANIM_animdata_freelist(&anim_data);
}

static void decimate_exit(bContext *C, wmOperator *op)
{
  tDecimateGraphOp *dgo = op->customdata;
  wmWindow *win = CTX_wm_window(C);

  /* If data exists, clear its data and exit. */
  if (dgo == NULL) {
    return;
  }

  ScrArea *area = dgo->area;
  LinkData *link;

  for (link = dgo->bezt_arr_list.first; link != NULL; link = link->next) {
    tBeztCopyData *copy = link->data;
    MEM_freeN(copy->bezt);
    MEM_freeN(link->data);
  }

  BLI_freelistN(&dgo->bezt_arr_list);
  MEM_freeN(dgo);

  /* Return to normal cursor and header status. */
  WM_cursor_modal_restore(win);
  ED_area_status_text(area, NULL);

  /* Cleanup. */
  op->customdata = NULL;
}

/* Draw a percentage indicator in header. */
static void decimate_draw_status_header(wmOperator *op, tDecimateGraphOp *dgo)
{
  char status_str[UI_MAX_DRAW_STR];
  char mode_str[32];

  strcpy(mode_str, TIP_("Decimate Keyframes"));

  if (hasNumInput(&dgo->num)) {
    char str_offs[NUM_STR_REP_LEN];

    outputNumInput(&dgo->num, str_offs, &dgo->scene->unit);

    BLI_snprintf(status_str, sizeof(status_str), "%s: %s", mode_str, str_offs);
  }
  else {
    float percentage = RNA_property_float_get(op->ptr, dgo->percentage_prop);
    BLI_snprintf(
        status_str, sizeof(status_str), "%s: %d %%", mode_str, (int)(percentage * 100.0f));
  }

  ED_area_status_text(dgo->area, status_str);
}

/* Calculate percentage based on position of mouse (we only use x-axis for now.
 * Since this is more convenient for users to do), and store new percentage value.
 */
static void decimate_mouse_update_percentage(tDecimateGraphOp *dgo,
                                             wmOperator *op,
                                             const wmEvent *event)
{
  float percentage = (event->x - dgo->region->winrct.xmin) / ((float)dgo->region->winx);
  RNA_property_float_set(op->ptr, dgo->percentage_prop, percentage);
}

static int graphkeys_decimate_invoke(bContext *C, wmOperator *op, const wmEvent *event)
{
  tDecimateGraphOp *dgo;

  WM_cursor_modal_set(CTX_wm_window(C), WM_CURSOR_EW_SCROLL);

  /* Init slide-op data. */
  dgo = op->customdata = MEM_callocN(sizeof(tDecimateGraphOp), "tDecimateGraphOp");

  /* Get editor data. */
  if (ANIM_animdata_get_context(C, &dgo->ac) == 0) {
    decimate_exit(C, op);
    return OPERATOR_CANCELLED;
  }

  dgo->percentage_prop = RNA_struct_find_property(op->ptr, "remove_ratio");

  dgo->scene = CTX_data_scene(C);
  dgo->area = CTX_wm_area(C);
  dgo->region = CTX_wm_region(C);

  /* Initialize percentage so that it will have the correct value before the first mouse move. */
  decimate_mouse_update_percentage(dgo, op, event);

  decimate_draw_status_header(op, dgo);

  /* Construct a list with the original bezt arrays so we can restore them during modal operation.
   */
  {
    ListBase anim_data = {NULL, NULL};
    bAnimContext *ac = &dgo->ac;
    bAnimListElem *ale;

    int filter;

    /* Filter data. */
    filter = (ANIMFILTER_DATA_VISIBLE | ANIMFILTER_CURVE_VISIBLE | ANIMFILTER_FOREDIT |
              ANIMFILTER_SEL | ANIMFILTER_NODUPLIS);
    ANIM_animdata_filter(ac, &anim_data, filter, ac->data, ac->datatype);

    /* Loop through filtered data and copy the curves. */
    for (ale = anim_data.first; ale; ale = ale->next) {
      FCurve *fcu = (FCurve *)ale->key_data;

      if (fcu->bezt == NULL) {
        /* This curve is baked, skip it. */
        continue;
      }

      const int arr_size = sizeof(BezTriple) * fcu->totvert;

      tBeztCopyData *copy = MEM_mallocN(sizeof(tBeztCopyData), "bezts_copy");
      BezTriple *bezts_copy = MEM_mallocN(arr_size, "bezts_copy_array");

      copy->tot_vert = fcu->totvert;
      memcpy(bezts_copy, fcu->bezt, arr_size);

      copy->bezt = bezts_copy;

      LinkData *link = NULL;

      link = MEM_callocN(sizeof(LinkData), "Bezt Link");
      link->data = copy;

      BLI_addtail(&dgo->bezt_arr_list, link);
    }

    ANIM_animdata_freelist(&anim_data);
  }

  if (dgo->bezt_arr_list.first == NULL) {
    WM_report(RPT_WARNING,
              "Fcurve Decimate: Can't decimate baked channels. Unbake them and try again.");
    decimate_exit(C, op);
    return OPERATOR_CANCELLED;
  }

  WM_event_add_modal_handler(C, op);
  return OPERATOR_RUNNING_MODAL;
}

static void graphkeys_decimate_modal_update(bContext *C, wmOperator *op)
{
  /* Perform decimate updates - in response to some user action
   * (e.g. pressing a key or moving the mouse). */
  tDecimateGraphOp *dgo = op->customdata;

  decimate_draw_status_header(op, dgo);

  /* Reset keyframe data (so we get back to the original state). */
  decimate_reset_bezts(dgo);

  /* Apply... */
  float remove_ratio = RNA_property_float_get(op->ptr, dgo->percentage_prop);
  /* We don't want to limit the decimation to a certain error margin. */
  const float error_sq_max = FLT_MAX;
  decimate_graph_keys(&dgo->ac, remove_ratio, error_sq_max);
  WM_event_add_notifier(C, NC_ANIMATION | ND_KEYFRAME | NA_EDITED, NULL);
}

static int graphkeys_decimate_modal(bContext *C, wmOperator *op, const wmEvent *event)
{
  /* This assumes that we are in "DECIM_RATIO" mode. This is because the error margin is very hard
   * and finicky to control with this modal mouse grab method. Therefore, it is expected that the
   * error margin mode is not adjusted by the modal operator but instead tweaked via the redo
   * panel.*/
  tDecimateGraphOp *dgo = op->customdata;

  const bool has_numinput = hasNumInput(&dgo->num);

  switch (event->type) {
    case LEFTMOUSE: /* Confirm */
    case EVT_RETKEY:
    case EVT_PADENTER: {
      if (event->val == KM_PRESS) {
        decimate_exit(C, op);

        return OPERATOR_FINISHED;
      }
      break;
    }

    case EVT_ESCKEY: /* Cancel */
    case RIGHTMOUSE: {
      if (event->val == KM_PRESS) {
        decimate_reset_bezts(dgo);

        WM_event_add_notifier(C, NC_ANIMATION | ND_KEYFRAME | NA_EDITED, NULL);

        decimate_exit(C, op);

        return OPERATOR_CANCELLED;
      }
      break;
    }

    /* Percentage Change... */
    case MOUSEMOVE: /* Calculate new position. */
    {
      if (has_numinput == false) {
        /* Update percentage based on position of mouse. */
        decimate_mouse_update_percentage(dgo, op, event);

        /* Update pose to reflect the new values. */
        graphkeys_decimate_modal_update(C, op);
      }
      break;
    }
    default: {
      if ((event->val == KM_PRESS) && handleNumInput(C, &dgo->num, event)) {
        float value;
        float percentage = RNA_property_float_get(op->ptr, dgo->percentage_prop);

        /* Grab percentage from numeric input, and store this new value for redo
         * NOTE: users see ints, while internally we use a 0-1 float.
         */
        value = percentage * 100.0f;
        applyNumInput(&dgo->num, &value);

        percentage = value / 100.0f;
        RNA_property_float_set(op->ptr, dgo->percentage_prop, percentage);

        /* Update decimate output to reflect the new values. */
        graphkeys_decimate_modal_update(C, op);
        break;
      }

      /* Unhandled event - maybe it was some view manip? */
      /* Allow to pass through. */
      return OPERATOR_RUNNING_MODAL | OPERATOR_PASS_THROUGH;
    }
  }

  return OPERATOR_RUNNING_MODAL;
}

static int graphkeys_decimate_exec(bContext *C, wmOperator *op)
{
  bAnimContext ac;

  /* Get editor data. */
  if (ANIM_animdata_get_context(C, &ac) == 0) {
    return OPERATOR_CANCELLED;
  }

  tDecimModes mode = RNA_enum_get(op->ptr, "mode");
  /* We want to be able to work on all available keyframes. */
  float remove_ratio = 1.0f;
  /* We don't want to limit the decimation to a certain error margin. */
  float error_sq_max = FLT_MAX;

  switch (mode) {
    case DECIM_RATIO:
      remove_ratio = RNA_float_get(op->ptr, "remove_ratio");
      break;
    case DECIM_ERROR:
      error_sq_max = RNA_float_get(op->ptr, "remove_error_margin");
      /* The decimate algorithm expects the error to be squared. */
      error_sq_max *= error_sq_max;

      break;
  }

  if (remove_ratio == 0.0f || error_sq_max == 0.0f) {
    /* Nothing to remove. */
    return OPERATOR_FINISHED;
  }

  decimate_graph_keys(&ac, remove_ratio, error_sq_max);

  /* Set notifier that keyframes have changed. */
  WM_event_add_notifier(C, NC_ANIMATION | ND_KEYFRAME | NA_EDITED, NULL);

  return OPERATOR_FINISHED;
}

static bool graphkeys_decimate_poll_property(const bContext *UNUSED(C),
                                             wmOperator *op,
                                             const PropertyRNA *prop)
{
  const char *prop_id = RNA_property_identifier(prop);

  if (STRPREFIX(prop_id, "remove")) {
    int mode = RNA_enum_get(op->ptr, "mode");

    if (STREQ(prop_id, "remove_ratio") && mode != DECIM_RATIO) {
      return false;
    }
    if (STREQ(prop_id, "remove_error_margin") && mode != DECIM_ERROR) {
      return false;
    }
  }

  return true;
}

static char *graphkeys_decimate_desc(bContext *UNUSED(C),
                                     wmOperatorType *UNUSED(op),
                                     PointerRNA *ptr)
{

  if (RNA_enum_get(ptr, "mode") == DECIM_ERROR) {
    return BLI_strdup(
        "Decimate F-Curves by specifying how much it can deviate from the original curve");
  }

  /* Use default description. */
  return NULL;
}

static const EnumPropertyItem decimate_mode_items[] = {
    {DECIM_RATIO,
     "RATIO",
     0,
     "Ratio",
     "Use a percentage to specify how many keyframes you want to remove"},
    {DECIM_ERROR,
     "ERROR",
     0,
     "Error Margin",
     "Use an error margin to specify how much the curve is allowed to deviate from the original "
     "path"},
    {0, NULL, 0, NULL, NULL},
};

void GRAPH_OT_decimate(wmOperatorType *ot)
{
  /* Identifiers */
  ot->name = "Decimate Keyframes";
  ot->idname = "GRAPH_OT_decimate";
  ot->description =
      "Decimate F-Curves by removing keyframes that influence the curve shape the least";

  /* API callbacks */
  ot->poll_property = graphkeys_decimate_poll_property;
  ot->get_description = graphkeys_decimate_desc;
  ot->invoke = graphkeys_decimate_invoke;
  ot->modal = graphkeys_decimate_modal;
  ot->exec = graphkeys_decimate_exec;
  ot->poll = graphop_editable_keyframes_poll;

  /* Flags */
  ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;

  /* Properties */
  RNA_def_enum(ot->srna,
               "mode",
               decimate_mode_items,
               DECIM_RATIO,
               "Mode",
               "Which mode to use for decimation");

  RNA_def_float_percentage(ot->srna,
                           "remove_ratio",
                           1.0f / 3.0f,
                           0.0f,
                           1.0f,
                           "Remove",
                           "The percentage of keyframes to remove",
                           0.0f,
                           1.0f);
  RNA_def_float(ot->srna,
                "remove_error_margin",
                0.0f,
                0.0f,
                FLT_MAX,
                "Max Error Margin",
                "How much the new decimated curve is allowed to deviate from the original",
                0.0f,
                10.0f);
}

/* ******************** Bake F-Curve Operator *********************** */
/* This operator bakes the data of the selected F-Curves to F-Points */

/* Bake each F-Curve into a set of samples. */
static void bake_graph_curves(bAnimContext *ac, int start, int end)
{
  ListBase anim_data = {NULL, NULL};
  bAnimListElem *ale;
  int filter;

  /* Filter data. */
  filter = (ANIMFILTER_DATA_VISIBLE | ANIMFILTER_CURVE_VISIBLE | ANIMFILTER_SEL |
            ANIMFILTER_FOREDIT | ANIMFILTER_NODUPLIS);
  ANIM_animdata_filter(ac, &anim_data, filter, ac->data, ac->datatype);

  /* Loop through filtered data and add keys between selected keyframes on every frame. */
  for (ale = anim_data.first; ale; ale = ale->next) {
    FCurve *fcu = (FCurve *)ale->key_data;
    ChannelDriver *driver = fcu->driver;

    /* Disable driver so that it don't muck up the sampling process. */
    fcu->driver = NULL;

    /* Create samples. */
    fcurve_store_samples(fcu, NULL, start, end, fcurve_samplingcb_evalcurve);

    /* Restore driver. */
    fcu->driver = driver;

    ale->update |= ANIM_UPDATE_DEPS;
  }

  ANIM_animdata_update(ac, &anim_data);
  ANIM_animdata_freelist(&anim_data);
}

/* ------------------- */

static int graphkeys_bake_exec(bContext *C, wmOperator *UNUSED(op))
{
  bAnimContext ac;
  Scene *scene = NULL;
  int start, end;

  /* Get editor data. */
  if (ANIM_animdata_get_context(C, &ac) == 0) {
    return OPERATOR_CANCELLED;
  }

  /* For now, init start/end from preview-range extents. */
  /* TODO: add properties for this. (Joshua Leung 2009) */
  scene = ac.scene;
  start = PSFRA;
  end = PEFRA;

  /* Bake keyframes. */
  bake_graph_curves(&ac, start, end);

  /* Set notifier that keyframes have changed. */
  /* NOTE: some distinction between order/number of keyframes and type should be made? */
  WM_event_add_notifier(C, NC_ANIMATION | ND_KEYFRAME | NA_EDITED, NULL);

  return OPERATOR_FINISHED;
}

void GRAPH_OT_bake(wmOperatorType *ot)
{
  /* Identifiers */
  ot->name = "Bake Curve";
  ot->idname = "GRAPH_OT_bake";
  ot->description = "Bake selected F-Curves to a set of sampled points defining a similar curve";

  /* API callbacks */
  ot->invoke = WM_operator_confirm; /* FIXME */
  ot->exec = graphkeys_bake_exec;
  ot->poll = graphop_selected_fcurve_poll;

  /* Flags */
  ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;

  /* TODO: add props for start/end frames (Joshua Leung 2009) */
}

/* ******************** Un-Bake F-Curve Operator *********************** */
/* This operator unbakes the data of the selected F-Points to F-Curves. */

/* Un-Bake F-Points into F-Curves. */
static void unbake_graph_curves(bAnimContext *ac, int start, int end)
{
  ListBase anim_data = {NULL, NULL};
  bAnimListElem *ale;

  /* Filter data. */
  const int filter = (ANIMFILTER_DATA_VISIBLE | ANIMFILTER_CURVE_VISIBLE | ANIMFILTER_SEL |
                      ANIMFILTER_FOREDIT | ANIMFILTER_NODUPLIS);
  ANIM_animdata_filter(ac, &anim_data, filter, ac->data, ac->datatype);

  /* Loop through filtered data and add keys between selected keyframes on every frame. */
  for (ale = anim_data.first; ale; ale = ale->next) {
    FCurve *fcu = (FCurve *)ale->key_data;

    fcurve_samples_to_keyframes(fcu, start, end);

    ale->update |= ANIM_UPDATE_DEPS;
  }

  ANIM_animdata_update(ac, &anim_data);
  ANIM_animdata_freelist(&anim_data);
}

/* ------------------- */

static int graphkeys_unbake_exec(bContext *C, wmOperator *UNUSED(op))
{
  bAnimContext ac;
  Scene *scene = NULL;
  int start, end;

  /* Get editor data. */
  if (ANIM_animdata_get_context(C, &ac) == 0) {
    return OPERATOR_CANCELLED;
  }

  scene = ac.scene;
  start = PSFRA;
  end = PEFRA;

  /* Unbake keyframes. */
  unbake_graph_curves(&ac, start, end);

  /* Set notifier that keyframes have changed. */
  /* NOTE: some distinction between order/number of keyframes and type should be made? */
  WM_event_add_notifier(C, NC_ANIMATION | ND_KEYFRAME | NA_EDITED, NULL);

  return OPERATOR_FINISHED;
}

void GRAPH_OT_unbake(wmOperatorType *ot)
{
  /* Identifiers */
  ot->name = "Un-Bake Curve";
  ot->idname = "GRAPH_OT_unbake";
  ot->description = "Un-Bake selected F-Points to F-Curves";

  /* API callbacks */
  ot->exec = graphkeys_unbake_exec;
  ot->poll = graphop_selected_fcurve_poll;

  /* Flags */
  ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;
}

#ifdef WITH_AUDASPACE

/* ******************** Sound Bake F-Curve Operator *********************** */
/* This operator bakes the given sound to the selected F-Curves */

/* ------------------- */

/* Custom data storage passed to the F-Sample-ing function,
 * which provides the necessary info for baking the sound.
 */
typedef struct tSoundBakeInfo {
  float *samples;
  int length;
  int cfra;
} tSoundBakeInfo;

/* ------------------- */

/* Sampling callback used to determine the value from the sound to
 * save in the F-Curve at the specified frame.
 */
static float fcurve_samplingcb_sound(FCurve *UNUSED(fcu), void *data, float evaltime)
{
  tSoundBakeInfo *sbi = (tSoundBakeInfo *)data;

  int position = evaltime - sbi->cfra;
  if ((position < 0) || (position >= sbi->length)) {
    return 0.0f;
  }

  return sbi->samples[position];
}

/* ------------------- */

static int graphkeys_sound_bake_exec(bContext *C, wmOperator *op)
{
  bAnimContext ac;
  ListBase anim_data = {NULL, NULL};
  bAnimListElem *ale;
  int filter;

  tSoundBakeInfo sbi;
  Scene *scene = NULL;
  int start, end;

  char path[FILE_MAX];

  /* Get editor data. */
  if (ANIM_animdata_get_context(C, &ac) == 0) {
    return OPERATOR_CANCELLED;
  }

  RNA_string_get(op->ptr, "filepath", path);

  if (!BLI_is_file(path)) {
    BKE_reportf(op->reports, RPT_ERROR, "File not found '%s'", path);
    return OPERATOR_CANCELLED;
  }

  scene = ac.scene; /* Current scene. */

  /* Store necessary data for the baking steps. */
  sbi.samples = AUD_readSoundBuffer(path,
                                    RNA_float_get(op->ptr, "low"),
                                    RNA_float_get(op->ptr, "high"),
                                    RNA_float_get(op->ptr, "attack"),
                                    RNA_float_get(op->ptr, "release"),
                                    RNA_float_get(op->ptr, "threshold"),
                                    RNA_boolean_get(op->ptr, "use_accumulate"),
                                    RNA_boolean_get(op->ptr, "use_additive"),
                                    RNA_boolean_get(op->ptr, "use_square"),
                                    RNA_float_get(op->ptr, "sthreshold"),
                                    FPS,
                                    &sbi.length);

  if (sbi.samples == NULL) {
    BKE_report(op->reports, RPT_ERROR, "Unsupported audio format");
    return OPERATOR_CANCELLED;
  }

  /* Determine extents of the baking. */
  sbi.cfra = start = CFRA;
  end = CFRA + sbi.length - 1;

  /* Filter anim channels. */
  filter = (ANIMFILTER_DATA_VISIBLE | ANIMFILTER_CURVE_VISIBLE | ANIMFILTER_SEL |
            ANIMFILTER_FOREDIT | ANIMFILTER_NODUPLIS);
  ANIM_animdata_filter(&ac, &anim_data, filter, ac.data, ac.datatype);

  /* Loop through all selected F-Curves, replacing its data with the sound samples. */
  for (ale = anim_data.first; ale; ale = ale->next) {
    FCurve *fcu = (FCurve *)ale->key_data;

    /* Sample the sound. */
    fcurve_store_samples(fcu, &sbi, start, end, fcurve_samplingcb_sound);

    ale->update |= ANIM_UPDATE_DEFAULT;
  }

  /* Free sample data. */
  free(sbi.samples);

  /* Validate keyframes after editing. */
  ANIM_animdata_update(&ac, &anim_data);
  ANIM_animdata_freelist(&anim_data);

  /* Set notifier that 'keyframes' have changed. */
  WM_event_add_notifier(C, NC_ANIMATION | ND_KEYFRAME | NA_EDITED, NULL);

  return OPERATOR_FINISHED;
}

#else /* WITH_AUDASPACE */

static int graphkeys_sound_bake_exec(bContext *UNUSED(C), wmOperator *op)
{
  BKE_report(op->reports, RPT_ERROR, "Compiled without sound support");

  return OPERATOR_CANCELLED;
}

#endif /* WITH_AUDASPACE */

static int graphkeys_sound_bake_invoke(bContext *C, wmOperator *op, const wmEvent *event)
{
  bAnimContext ac;

  /* Verify editor data. */
  if (ANIM_animdata_get_context(C, &ac) == 0) {
    return OPERATOR_CANCELLED;
  }

  return WM_operator_filesel(C, op, event);
}

void GRAPH_OT_sound_bake(wmOperatorType *ot)
{
  /* Identifiers */
  ot->name = "Bake Sound to F-Curves";
  ot->idname = "GRAPH_OT_sound_bake";
  ot->description = "Bakes a sound wave to selected F-Curves";

  /* API callbacks */
  ot->invoke = graphkeys_sound_bake_invoke;
  ot->exec = graphkeys_sound_bake_exec;
  ot->poll = graphop_selected_fcurve_poll;

  /* Flags */
  ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;

  /* Properties */
  WM_operator_properties_filesel(ot,
                                 FILE_TYPE_FOLDER | FILE_TYPE_SOUND | FILE_TYPE_MOVIE,
                                 FILE_SPECIAL,
                                 FILE_OPENFILE,
                                 WM_FILESEL_FILEPATH | WM_FILESEL_SHOW_PROPS,
                                 FILE_DEFAULTDISPLAY,
                                 FILE_SORT_DEFAULT);
  RNA_def_float(ot->srna,
                "low",
                0.0f,
                0.0,
                100000.0,
                "Lowest frequency",
                "Cutoff frequency of a high-pass filter that is applied to the audio data",
                0.1,
                1000.00);
  RNA_def_float(ot->srna,
                "high",
                100000.0,
                0.0,
                100000.0,
                "Highest frequency",
                "Cutoff frequency of a low-pass filter that is applied to the audio data",
                0.1,
                1000.00);
  RNA_def_float(ot->srna,
                "attack",
                0.005,
                0.0,
                2.0,
                "Attack time",
                "Value for the hull curve calculation that tells how fast the hull curve can rise "
                "(the lower the value the steeper it can rise)",
                0.01,
                0.1);
  RNA_def_float(ot->srna,
                "release",
                0.2,
                0.0,
                5.0,
                "Release time",
                "Value for the hull curve calculation that tells how fast the hull curve can fall "
                "(the lower the value the steeper it can fall)",
                0.01,
                0.2);
  RNA_def_float(ot->srna,
                "threshold",
                0.0,
                0.0,
                1.0,
                "Threshold",
                "Minimum amplitude value needed to influence the hull curve",
                0.01,
                0.1);
  RNA_def_boolean(ot->srna,
                  "use_accumulate",
                  0,
                  "Accumulate",
                  "Only the positive differences of the hull curve amplitudes are summarized to "
                  "produce the output");
  RNA_def_boolean(
      ot->srna,
      "use_additive",
      0,
      "Additive",
      "The amplitudes of the hull curve are summarized (or, when Accumulate is enabled, "
      "both positive and negative differences are accumulated)");
  RNA_def_boolean(ot->srna,
                  "use_square",
                  0,
                  "Square",
                  "The output is a square curve (negative values always result in -1, and "
                  "positive ones in 1)");
  RNA_def_float(ot->srna,
                "sthreshold",
                0.1,
                0.0,
                1.0,
                "Square Threshold",
                "Square only: all values with an absolute amplitude lower than that result in 0",
                0.01,
                0.1);
}

/* ******************** Sample Keyframes Operator *********************** */
/* This operator 'bakes' the values of the curve into new keyframes between pairs
 * of selected keyframes. It is useful for creating keyframes for tweaking overlap.
 */

/* Evaluates the curves between each selected keyframe on each frame, and keys the value. */
static void sample_graph_keys(bAnimContext *ac)
{
  ListBase anim_data = {NULL, NULL};
  bAnimListElem *ale;
  int filter;

  /* filter data */
  filter = (ANIMFILTER_DATA_VISIBLE | ANIMFILTER_CURVE_VISIBLE | ANIMFILTER_FOREDIT |
            ANIMFILTER_NODUPLIS);
  ANIM_animdata_filter(ac, &anim_data, filter, ac->data, ac->datatype);

  /* Loop through filtered data and add keys between selected keyframes on every frame. */
  for (ale = anim_data.first; ale; ale = ale->next) {
    sample_fcurve((FCurve *)ale->key_data);

    ale->update |= ANIM_UPDATE_DEPS;
  }

  ANIM_animdata_update(ac, &anim_data);
  ANIM_animdata_freelist(&anim_data);
}

/* ------------------- */

static int graphkeys_sample_exec(bContext *C, wmOperator *UNUSED(op))
{
  bAnimContext ac;

  /* Get editor data. */
  if (ANIM_animdata_get_context(C, &ac) == 0) {
    return OPERATOR_CANCELLED;
  }

  /* Sample keyframes. */
  sample_graph_keys(&ac);

  /* Set notifier that keyframes have changed. */
  WM_event_add_notifier(C, NC_ANIMATION | ND_KEYFRAME | NA_EDITED, NULL);

  return OPERATOR_FINISHED;
}

void GRAPH_OT_sample(wmOperatorType *ot)
{
  /* Identifiers */
  ot->name = "Sample Keyframes";
  ot->idname = "GRAPH_OT_sample";
  ot->description = "Add keyframes on every frame between the selected keyframes";

  /* API callbacks */
  ot->exec = graphkeys_sample_exec;
  ot->poll = graphop_editable_keyframes_poll;

  /* Flags */
  ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;
}

/* ************************************************************************** */
/* SETTINGS STUFF */

/* ******************** Set Extrapolation-Type Operator *********************** */

/* Defines for make/clear cyclic extrapolation tools. */
#define MAKE_CYCLIC_EXPO -1
#define CLEAR_CYCLIC_EXPO -2

/* Defines for set extrapolation-type for selected keyframes tool. */
static const EnumPropertyItem prop_graphkeys_expo_types[] = {
    {FCURVE_EXTRAPOLATE_CONSTANT,
     "CONSTANT",
     0,
     "Constant Extrapolation",
     "Values on endpoint keyframes are held"},
    {FCURVE_EXTRAPOLATE_LINEAR,
     "LINEAR",
     0,
     "Linear Extrapolation",
     "Straight-line slope of end segments are extended past the endpoint keyframes"},

    {MAKE_CYCLIC_EXPO,
     "MAKE_CYCLIC",
     0,
     "Make Cyclic (F-Modifier)",
     "Add Cycles F-Modifier if one doesn't exist already"},
    {CLEAR_CYCLIC_EXPO,
     "CLEAR_CYCLIC",
     0,
     "Clear Cyclic (F-Modifier)",
     "Remove Cycles F-Modifier if not needed anymore"},
    {0, NULL, 0, NULL, NULL},
};

/* This function is responsible for setting extrapolation mode for keyframes. */
static void setexpo_graph_keys(bAnimContext *ac, short mode)
{
  ListBase anim_data = {NULL, NULL};
  bAnimListElem *ale;
  int filter;

  /* Filter data. */
  filter = (ANIMFILTER_DATA_VISIBLE | ANIMFILTER_CURVE_VISIBLE | ANIMFILTER_SEL |
            ANIMFILTER_FOREDIT | ANIMFILTER_NODUPLIS);
  ANIM_animdata_filter(ac, &anim_data, filter, ac->data, ac->datatype);

  /* Loop through setting mode per F-Curve. */
  for (ale = anim_data.first; ale; ale = ale->next) {
    FCurve *fcu = (FCurve *)ale->data;

    if (mode >= 0) {
      /* Just set mode setting. */
      fcu->extend = mode;

      ale->update |= ANIM_UPDATE_HANDLES;
    }
    else {
      /* Shortcuts for managing Cycles F-Modifiers to make it easier to toggle cyclic animation
       * without having to go through FModifier UI in Graph Editor to do so.
       */
      if (mode == MAKE_CYCLIC_EXPO) {
        /* Only add if one doesn't exist. */
        if (list_has_suitable_fmodifier(&fcu->modifiers, FMODIFIER_TYPE_CYCLES, -1) == 0) {
          /* TODO: add some more preset versions which set different extrapolation options?
           * (Joshua Leung 2011) */
          add_fmodifier(&fcu->modifiers, FMODIFIER_TYPE_CYCLES, fcu);
        }
      }
      else if (mode == CLEAR_CYCLIC_EXPO) {
        /* Remove all the modifiers fitting this description. */
        FModifier *fcm, *fcn = NULL;

        for (fcm = fcu->modifiers.first; fcm; fcm = fcn) {
          fcn = fcm->next;

          if (fcm->type == FMODIFIER_TYPE_CYCLES) {
            remove_fmodifier(&fcu->modifiers, fcm);
          }
        }
      }
    }

    ale->update |= ANIM_UPDATE_DEPS;
  }

  ANIM_animdata_update(ac, &anim_data);
  ANIM_animdata_freelist(&anim_data);
}

/* ------------------- */

static int graphkeys_expo_exec(bContext *C, wmOperator *op)
{
  bAnimContext ac;
  short mode;

  /* Get editor data. */
  if (ANIM_animdata_get_context(C, &ac) == 0) {
    return OPERATOR_CANCELLED;
  }

  /* Get handle setting mode. */
  mode = RNA_enum_get(op->ptr, "type");

  /* Set handle type. */
  setexpo_graph_keys(&ac, mode);

  /* Set notifier that keyframe properties have changed. */
  WM_event_add_notifier(C, NC_ANIMATION | ND_KEYFRAME_PROP, NULL);

  return OPERATOR_FINISHED;
}

void GRAPH_OT_extrapolation_type(wmOperatorType *ot)
{
  /* Identifiers */
  ot->name = "Set Keyframe Extrapolation";
  ot->idname = "GRAPH_OT_extrapolation_type";
  ot->description = "Set extrapolation mode for selected F-Curves";

  /* API callbacks */
  ot->invoke = WM_menu_invoke;
  ot->exec = graphkeys_expo_exec;
  ot->poll = graphop_editable_keyframes_poll;

  /* Flags */
  ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;

  /* Id-props */
  ot->prop = RNA_def_enum(ot->srna, "type", prop_graphkeys_expo_types, 0, "Type", "");
}

/* ******************** Set Interpolation-Type Operator *********************** */

/* This function is responsible for setting interpolation mode for keyframes. */
static void setipo_graph_keys(bAnimContext *ac, short mode)
{
  ListBase anim_data = {NULL, NULL};
  bAnimListElem *ale;
  int filter;
  KeyframeEditFunc set_cb = ANIM_editkeyframes_ipo(mode);

  /* Filter data. */
  filter = (ANIMFILTER_DATA_VISIBLE | ANIMFILTER_CURVE_VISIBLE | ANIMFILTER_FOREDIT |
            ANIMFILTER_NODUPLIS);
  ANIM_animdata_filter(ac, &anim_data, filter, ac->data, ac->datatype);

  /* Loop through setting BezTriple interpolation
   * Note: we do not supply KeyframeEditData to the looper yet.
   * Currently that's not necessary here.
   */
  for (ale = anim_data.first; ale; ale = ale->next) {
    ANIM_fcurve_keyframes_loop(NULL, ale->key_data, NULL, set_cb, calchandles_fcurve);

    ale->update |= ANIM_UPDATE_DEFAULT_NOHANDLES;
  }

  ANIM_animdata_update(ac, &anim_data);
  ANIM_animdata_freelist(&anim_data);
}

/* ------------------- */

static int graphkeys_ipo_exec(bContext *C, wmOperator *op)
{
  bAnimContext ac;
  short mode;

  /* Get editor data. */
  if (ANIM_animdata_get_context(C, &ac) == 0) {
    return OPERATOR_CANCELLED;
  }

  /* Get handle setting mode. */
  mode = RNA_enum_get(op->ptr, "type");

  /* Set handle type. */
  setipo_graph_keys(&ac, mode);

  /* Set notifier that keyframe properties have changed. */
  WM_event_add_notifier(C, NC_ANIMATION | ND_KEYFRAME_PROP, NULL);

  return OPERATOR_FINISHED;
}

void GRAPH_OT_interpolation_type(wmOperatorType *ot)
{
  /* Identifiers */
  ot->name = "Set Keyframe Interpolation";
  ot->idname = "GRAPH_OT_interpolation_type";
  ot->description =
      "Set interpolation mode for the F-Curve segments starting from the selected keyframes";

  /* API callbacks */
  ot->invoke = WM_menu_invoke;
  ot->exec = graphkeys_ipo_exec;
  ot->poll = graphop_editable_keyframes_poll;

  /* Flags */
  ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;

  /* Id-props */
  ot->prop = RNA_def_enum(
      ot->srna, "type", rna_enum_beztriple_interpolation_mode_items, 0, "Type", "");
}

/* ******************** Set Easing Operator *********************** */

static void seteasing_graph_keys(bAnimContext *ac, short mode)
{
  ListBase anim_data = {NULL, NULL};
  bAnimListElem *ale;
  int filter;
  KeyframeEditFunc set_cb = ANIM_editkeyframes_easing(mode);

  /* Filter data. */
  filter = (ANIMFILTER_DATA_VISIBLE | ANIMFILTER_CURVE_VISIBLE | ANIMFILTER_FOREDIT |
            ANIMFILTER_NODUPLIS);
  ANIM_animdata_filter(ac, &anim_data, filter, ac->data, ac->datatype);

  /* Loop through setting BezTriple easing.
   * Note: we do not supply KeyframeEditData to the looper yet.
   * Currently that's not necessary here.
   */
  for (ale = anim_data.first; ale; ale = ale->next) {
    ANIM_fcurve_keyframes_loop(NULL, ale->key_data, NULL, set_cb, calchandles_fcurve);

    ale->update |= ANIM_UPDATE_DEFAULT_NOHANDLES;
  }

  ANIM_animdata_update(ac, &anim_data);
  ANIM_animdata_freelist(&anim_data);
}

static int graphkeys_easing_exec(bContext *C, wmOperator *op)
{
  bAnimContext ac;
  short mode;

  /* Get editor data. */
  if (ANIM_animdata_get_context(C, &ac) == 0) {
    return OPERATOR_CANCELLED;
  }

  /* Get handle setting mode. */
  mode = RNA_enum_get(op->ptr, "type");

  /* Set handle type. */
  seteasing_graph_keys(&ac, mode);

  /* Set notifier that keyframe properties have changed. */
  WM_event_add_notifier(C, NC_ANIMATION | ND_KEYFRAME_PROP, NULL);

  return OPERATOR_FINISHED;
}

void GRAPH_OT_easing_type(wmOperatorType *ot)
{
  /* Identifiers */
  ot->name = "Set Keyframe Easing Type";
  ot->idname = "GRAPH_OT_easing_type";
  ot->description =
      "Set easing type for the F-Curve segments starting from the selected keyframes";

  /* API callbacks */
  ot->invoke = WM_menu_invoke;
  ot->exec = graphkeys_easing_exec;
  ot->poll = graphop_editable_keyframes_poll;

  /* Flags */
  ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;

  /* Id-props */
  ot->prop = RNA_def_enum(
      ot->srna, "type", rna_enum_beztriple_interpolation_easing_items, 0, "Type", "");
}

/* ******************** Set Handle-Type Operator *********************** */

/* This function is responsible for setting handle-type of selected keyframes. */
static void sethandles_graph_keys(bAnimContext *ac, short mode)
{
  ListBase anim_data = {NULL, NULL};
  bAnimListElem *ale;
  int filter;

  KeyframeEditFunc edit_cb = ANIM_editkeyframes_handles(mode);
  KeyframeEditFunc sel_cb = ANIM_editkeyframes_ok(BEZT_OK_SELECTED);

  /* Filter data. */
  filter = (ANIMFILTER_DATA_VISIBLE | ANIMFILTER_CURVE_VISIBLE | ANIMFILTER_FOREDIT |
            ANIMFILTER_NODUPLIS);
  ANIM_animdata_filter(ac, &anim_data, filter, ac->data, ac->datatype);

  /* Loop through setting flags for handles.
   * Note: we do not supply KeyframeEditData to the looper yet.
   * Currently that's not necessary here.
   */
  for (ale = anim_data.first; ale; ale = ale->next) {
    FCurve *fcu = (FCurve *)ale->key_data;

    /* Any selected keyframes for editing? */
    if (ANIM_fcurve_keyframes_loop(NULL, fcu, NULL, sel_cb, NULL)) {
      /* Change type of selected handles. */
      ANIM_fcurve_keyframes_loop(NULL, fcu, NULL, edit_cb, calchandles_fcurve);

      ale->update |= ANIM_UPDATE_DEFAULT;
    }
  }

  ANIM_animdata_update(ac, &anim_data);
  ANIM_animdata_freelist(&anim_data);
}
/* ------------------- */

static int graphkeys_handletype_exec(bContext *C, wmOperator *op)
{
  bAnimContext ac;
  short mode;

  /* Get editor data. */
  if (ANIM_animdata_get_context(C, &ac) == 0) {
    return OPERATOR_CANCELLED;
  }

  /* Get handle setting mode. */
  mode = RNA_enum_get(op->ptr, "type");

  /* Set handle type. */
  sethandles_graph_keys(&ac, mode);

  /* Set notifier that keyframe properties have changed. */
  WM_event_add_notifier(C, NC_ANIMATION | ND_KEYFRAME_PROP, NULL);

  return OPERATOR_FINISHED;
}

void GRAPH_OT_handle_type(wmOperatorType *ot)
{
  /* Identifiers */
  ot->name = "Set Keyframe Handle Type";
  ot->idname = "GRAPH_OT_handle_type";
  ot->description = "Set type of handle for selected keyframes";

  /* API callbacks */
  ot->invoke = WM_menu_invoke;
  ot->exec = graphkeys_handletype_exec;
  ot->poll = graphop_editable_keyframes_poll;

  /* Flags */
  ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;

  /* Id-props */
  ot->prop = RNA_def_enum(ot->srna, "type", rna_enum_keyframe_handle_type_items, 0, "Type", "");
}

/* ************************************************************************** */
/* TRANSFORM STUFF */

/* ***************** 'Euler Filter' Operator **************************** */
/* Euler filter tools (as seen in Maya), are necessary for working with 'baked'
 * rotation curves (with Euler rotations). The main purpose of such tools is to
 * resolve any discontinuities that may arise in the curves due to the clamping
 * of values to -180 degrees to 180 degrees.
 */

/* Set of three euler-rotation F-Curves. */
typedef struct tEulerFilter {
  struct tEulerFilter *next, *prev;

  /** ID-block which owns the channels */
  ID *id;
  /** 3 Pointers to F-Curves. */
  FCurve *(fcurves[3]);
  /** Pointer to one of the RNA Path's used by one of the F-Curves. */
  const char *rna_path;
} tEulerFilter;

/* Find groups of `rotation_euler` channels. */
static ListBase /*tEulerFilter*/ euler_filter_group_channels(
    const ListBase /*bAnimListElem*/ *anim_data, ReportList *reports, int *r_num_groups)
{
  ListBase euler_groups = {NULL, NULL};
  tEulerFilter *euf = NULL;
  *r_num_groups = 0;

  LISTBASE_FOREACH (bAnimListElem *, ale, anim_data) {
    FCurve *const fcu = (FCurve *)ale->data;

    /* Check if this is an appropriate F-Curve:
     * - Only rotation curves.
     * - For pchan curves, make sure we're only using the euler curves.
     */
    if (strstr(fcu->rna_path, "rotation_euler") == NULL) {
      continue;
    }
    if (ELEM(fcu->array_index, 0, 1, 2) == 0) {
      BKE_reportf(reports,
                  RPT_WARNING,
                  "Euler Rotation F-Curve has invalid index (ID='%s', Path='%s', Index=%d)",
                  (ale->id) ? ale->id->name : TIP_("<No ID>"),
                  fcu->rna_path,
                  fcu->array_index);
      continue;
    }

    /* Assume that this animation channel will be touched by the Euler filter. Doing this here
     * saves another loop over the animation data. */
    ale->update |= ANIM_UPDATE_DEFAULT;

    /* Optimization: assume that xyz curves will always be stored consecutively,
     * so if the paths or the ID's don't match up, then a curve needs to be added
     * to a new group.
     */
    if ((euf) && (euf->id == ale->id) && (STREQ(euf->rna_path, fcu->rna_path))) {
      /* This should be fine to add to the existing group then. */
      euf->fcurves[fcu->array_index] = fcu;
      continue;
    }

    /* Just add to a new block. */
    euf = MEM_callocN(sizeof(tEulerFilter), "tEulerFilter");
    BLI_addtail(&euler_groups, euf);
    ++*r_num_groups;

    euf->id = ale->id;
    /* This should be safe, since we're only using it for a short time. */
    euf->rna_path = fcu->rna_path;
    euf->fcurves[fcu->array_index] = fcu;
  }

  return euler_groups;
}

/* Perform discontinuity filter based on conversion to quaternion and back.
 * Return true if successful, false otherwise. */
static bool euler_filter_quaternion(tEulerFilter *euf, ReportList *reports)
{
  /* Sanity check: ensure that there are enough F-Curves to work on in this group. */
  /* TODO: also enforce assumption that there be a full set of keyframes
   * at each position by ensuring that totvert counts are same? (Joshua Leung 2011) */
  if (ELEM(NULL, euf->fcurves[0], euf->fcurves[1], euf->fcurves[2])) {
    /* Report which components are missing. */
    BKE_reportf(reports,
                RPT_INFO,
                "Missing %s%s%s component(s) of euler rotation for ID='%s' and RNA-Path='%s'",
                (euf->fcurves[0] == NULL) ? "X" : "",
                (euf->fcurves[1] == NULL) ? "Y" : "",
                (euf->fcurves[2] == NULL) ? "Z" : "",
                euf->id->name,
                euf->rna_path);
    return false;
  }

  FCurve *fcu_rot_x = euf->fcurves[0];
  FCurve *fcu_rot_y = euf->fcurves[1];
  FCurve *fcu_rot_z = euf->fcurves[2];
  if (fcu_rot_x->totvert != fcu_rot_y->totvert || fcu_rot_y->totvert != fcu_rot_z->totvert) {
    /* Report which components are missing. */
    BKE_reportf(reports,
                RPT_INFO,
                "XYZ rotations not equally keyed for ID='%s' and RNA-Path='%s'",
                euf->id->name,
                euf->rna_path);

    return false;
  }

  if (fcu_rot_x->totvert < 2) {
    /* Single rotations are trivially "filtered". */
    return true;
  }

  float last_euler[3] = {
      fcu_rot_x->bezt[0].vec[1][1],
      fcu_rot_y->bezt[0].vec[1][1],
      fcu_rot_z->bezt[0].vec[1][1],
  };

  for (int keyframe_index = 1; keyframe_index < fcu_rot_x->totvert; ++keyframe_index) {
    /* TODO(Sybren): check X-coordinates of keyframes to ensure they're on the same frame, and we
     * don't accidentally just have the same number of keyframes but on different frames. */
    const float euler[3] = {
        fcu_rot_x->bezt[keyframe_index].vec[1][1],
        fcu_rot_y->bezt[keyframe_index].vec[1][1],
        fcu_rot_z->bezt[keyframe_index].vec[1][1],
    };

    /* TODO(Sybren): Quaternions are nice, but the calls below internally use rotation matrices.
     * Directly using matrices here may speed things up a bit. */
    float quaternion[4];
    eul_to_quat(quaternion, euler);
    quat_to_compatible_eul(last_euler, last_euler, quaternion);

    /* Update the FCurves to have the new rotation values. */
    BKE_fcurve_keyframe_move_value_with_handles(&fcu_rot_x->bezt[keyframe_index], last_euler[0]);
    BKE_fcurve_keyframe_move_value_with_handles(&fcu_rot_y->bezt[keyframe_index], last_euler[1]);
    BKE_fcurve_keyframe_move_value_with_handles(&fcu_rot_z->bezt[keyframe_index], last_euler[2]);
  }
  return true;
}

static bool euler_filter_single_channel(FCurve *fcu)
{
}

static int euler_filter_perform_filter(ListBase /*tEulerFilter*/ *eulers, ReportList *reports)
{
  int failed = 0;

  LISTBASE_FOREACH (tEulerFilter *, euf, eulers) {
    if (!euler_filter_quaternion(euf)) {
      ++failed;
    }
  }

  return failed;
}

static int graphkeys_euler_filter_exec(bContext *C, wmOperator *op)
{
  /* Get editor data. */
  bAnimContext ac;
  if (ANIM_animdata_get_context(C, &ac) == 0) {
    return OPERATOR_CANCELLED;
  }

  /* The process is done in two passes:
   * 1) Sets of three related rotation curves are identified from the selected channels,
   *    and are stored as a single 'operation unit' for the next step.
   * 2) Each set of three F-Curves is processed for each keyframe, with the values being
   *    processed as necessary.
   */

  /* Step 1: extract only the rotation f-curves. */
  const int filter = (ANIMFILTER_DATA_VISIBLE | ANIMFILTER_SEL | ANIMFILTER_CURVE_VISIBLE |
                      ANIMFILTER_FOREDIT | ANIMFILTER_NODUPLIS);
  ListBase anim_data = {NULL, NULL};
  ANIM_animdata_filter(&ac, &anim_data, filter, ac.data, ac.datatype);

  int groups = 0;
  ListBase eulers = euler_filter_group_channels(&anim_data, op->reports, &groups);
  BLI_assert(BLI_listbase_count(&eulers) == groups);

  if (groups == 0) {
    ANIM_animdata_freelist(&anim_data);
    BKE_report(op->reports, RPT_WARNING, "No Euler Rotation F-Curves to fix up");
    return OPERATOR_CANCELLED;
  }

  /* Step 2: go through each set of curves, processing the values at each keyframe.
   * - It is assumed that there must be a full set of keyframes at each keyframe position.
   */
  const int failed = euler_filter_perform_filter(&eulers, op->reports);
  BLI_freelistN(&eulers);

  ANIM_animdata_update(&ac, &anim_data);
  ANIM_animdata_freelist(&anim_data);

  /* Updates + finishing warnings. */
  if (failed == groups) {
    /* Every single rotation group we found went wrong. */
    BKE_report(
        op->reports,
        RPT_ERROR,
        "No Euler Rotations could be corrected, ensure each rotation has keys for all components, "
        "and that F-Curves for these are in consecutive XYZ order and selected");
    return OPERATOR_CANCELLED;
  }

  if (failed) {
    /* This means there were some successes as well. */
    BKE_report(
        op->reports,
        RPT_WARNING,
        "Some Euler Rotations could not be corrected due to missing/unselected/out-of-order "
        "F-Curves, ensure each rotation has keys for all components, and that F-Curves for these "
        "are in consecutive XYZ order and selected");
  }

  /* Set notifier that keyframes have changed. */
  WM_event_add_notifier(C, NC_ANIMATION | ND_KEYFRAME | NA_EDITED, NULL);

  /* Done at last. */
  return OPERATOR_FINISHED;
}

void GRAPH_OT_euler_filter(wmOperatorType *ot)
{
  /* Identifiers */
  ot->name = "Euler Discontinuity Filter";
  ot->idname = "GRAPH_OT_euler_filter";
  ot->description =
      "Fix large jumps and flips in the selected "
      "Euler Rotation F-Curves arising from rotation "
      "values being clipped when baking physics";

  /* API callbacks */
  ot->exec = graphkeys_euler_filter_exec;
  ot->poll = graphop_editable_keyframes_poll;

  /* Flags */
  ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;
}

/* ***************** Jump to Selected Frames Operator *********************** */

static bool graphkeys_framejump_poll(bContext *C)
{
  /* Prevent changes during render. */
  if (G.is_rendering) {
    return false;
  }

  return graphop_visible_keyframes_poll(C);
}

static KeyframeEditData sum_selected_keyframes(bAnimContext *ac)
{
  ListBase anim_data = {NULL, NULL};
  bAnimListElem *ale;
  int filter;
  KeyframeEditData ked;

  /* Init edit data. */
  memset(&ked, 0, sizeof(KeyframeEditData));

  /* Loop over action data, averaging values. */
  filter = (ANIMFILTER_DATA_VISIBLE | ANIMFILTER_CURVE_VISIBLE | ANIMFILTER_NODUPLIS);
  ANIM_animdata_filter(ac, &anim_data, filter, ac->data, ac->datatype);

  for (ale = anim_data.first; ale; ale = ale->next) {
    AnimData *adt = ANIM_nla_mapping_get(ac, ale);
    short mapping_flag = ANIM_get_normalization_flags(ac);
    KeyframeEditData current_ked;
    float offset;
    float unit_scale = ANIM_unit_mapping_get_factor(
        ac->scene, ale->id, ale->key_data, mapping_flag | ANIM_UNITCONV_ONLYKEYS, &offset);

    memset(&current_ked, 0, sizeof(current_ked));

    if (adt) {
      ANIM_nla_mapping_apply_fcurve(adt, ale->key_data, 0, 1);
      ANIM_fcurve_keyframes_loop(&current_ked, ale->key_data, NULL, bezt_calc_average, NULL);
      ANIM_nla_mapping_apply_fcurve(adt, ale->key_data, 1, 1);
    }
    else {
      ANIM_fcurve_keyframes_loop(&current_ked, ale->key_data, NULL, bezt_calc_average, NULL);
    }

    ked.f1 += current_ked.f1;
    ked.i1 += current_ked.i1;
    ked.f2 += (current_ked.f2 + offset) * unit_scale;
    ked.i2 += current_ked.i2;
  }

  ANIM_animdata_freelist(&anim_data);

  return ked;
}

/* Snap current-frame indicator to 'average time' of selected keyframe. */
static int graphkeys_framejump_exec(bContext *C, wmOperator *UNUSED(op))
{
  bAnimContext ac;

  /* Get editor data. */
  if (ANIM_animdata_get_context(C, &ac) == 0) {
    return OPERATOR_CANCELLED;
  }

  const KeyframeEditData keyframe_sum = sum_selected_keyframes(&ac);
  const float sum_time = keyframe_sum.f1;
  const float sum_value = keyframe_sum.f2;
  const int num_keyframes = keyframe_sum.i1;

  if (num_keyframes == 0) {
    return OPERATOR_FINISHED;
  }

  /* Set the new current frame and cursor values, based on the average time and value. */
  SpaceGraph *sipo = (SpaceGraph *)ac.sl;
  Scene *scene = ac.scene;

  /* Take the average values, rounding to the nearest int as necessary for int results. */
  if (sipo->mode == SIPO_MODE_DRIVERS) {
    /* Drivers Mode - Affects cursor (float) */
    sipo->cursorTime = sum_time / (float)num_keyframes;
  }
  else {
    /* Animation Mode - Affects current frame (int) */
    CFRA = round_fl_to_int(sum_time / num_keyframes);
    SUBFRA = 0.0f;
  }
  sipo->cursorVal = sum_value / (float)num_keyframes;

  /* Set notifier that things have changed. */
  WM_event_add_notifier(C, NC_SCENE | ND_FRAME, ac.scene);

  return OPERATOR_FINISHED;
}

void GRAPH_OT_frame_jump(wmOperatorType *ot)
{
  /* Identifiers */
  ot->name = "Jump to Keyframes";
  ot->idname = "GRAPH_OT_frame_jump";
  ot->description = "Place the cursor on the midpoint of selected keyframes";

  /* API callbacks */
  ot->exec = graphkeys_framejump_exec;
  ot->poll = graphkeys_framejump_poll;

  /* Flags */
  ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;
}

/* snap 2D cursor value to the average value of selected keyframe */
static int graphkeys_snap_cursor_value_exec(bContext *C, wmOperator *UNUSED(op))
{
  bAnimContext ac;

  if (ANIM_animdata_get_context(C, &ac) == 0) {
    return OPERATOR_CANCELLED;
  }

  const KeyframeEditData keyframe_sum = sum_selected_keyframes(&ac);
  const float sum_value = keyframe_sum.f2;
  const int num_keyframes = keyframe_sum.i1;

  if (num_keyframes == 0) {
    return OPERATOR_FINISHED;
  }

  SpaceGraph *sipo = (SpaceGraph *)ac.sl;
  sipo->cursorVal = sum_value / (float)num_keyframes;
  // WM_event_add_notifier(C, NC_SCENE | ND_FRAME, ac.scene);
  ED_region_tag_redraw(CTX_wm_region(C));

  return OPERATOR_FINISHED;
}

void GRAPH_OT_snap_cursor_value(wmOperatorType *ot)
{
  /* Identifiers. */
  ot->name = "Snap Cursor Value to Selected";
  ot->idname = "GRAPH_OT_snap_cursor_value";
  ot->description = "Place the cursor value on the average value of selected keyframes";

  /* API callbacks. */
  ot->exec = graphkeys_snap_cursor_value_exec;
  ot->poll = graphkeys_framejump_poll;

  /* Flags */
  ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;
}

/* ******************** Snap Keyframes Operator *********************** */

/* Defines for snap keyframes tool. */
static const EnumPropertyItem prop_graphkeys_snap_types[] = {
    {GRAPHKEYS_SNAP_CFRA,
     "CFRA",
     0,
     "Selection to Current Frame",
     "Snap selected keyframes to the current frame"},
    {GRAPHKEYS_SNAP_VALUE,
     "VALUE",
     0,
     "Selection to Cursor Value",
     "Set values of selected keyframes to the cursor value (Y/Horizontal component)"},
    {GRAPHKEYS_SNAP_NEAREST_FRAME,
     "NEAREST_FRAME",
     0,
     "Selection to Nearest Frame",
     "Snap selected keyframes to the nearest (whole) frame (use to fix accidental sub-frame "
     "offsets)"},
    {GRAPHKEYS_SNAP_NEAREST_SECOND,
     "NEAREST_SECOND",
     0,
     "Selection to Nearest Second",
     "Snap selected keyframes to the nearest second"},
    {GRAPHKEYS_SNAP_NEAREST_MARKER,
     "NEAREST_MARKER",
     0,
     "Selection to Nearest Marker",
     "Snap selected keyframes to the nearest marker"},
    {GRAPHKEYS_SNAP_HORIZONTAL,
     "HORIZONTAL",
     0,
     "Flatten Handles",
     "Flatten handles for a smoother transition"},
    {0, NULL, 0, NULL, NULL},
};

/* This function is responsible for snapping keyframes to frame-times. */
static void snap_graph_keys(bAnimContext *ac, short mode)
{
  ListBase anim_data = {NULL, NULL};
  bAnimListElem *ale;
  int filter;

  SpaceGraph *sipo = (SpaceGraph *)ac->sl;
  KeyframeEditData ked;
  KeyframeEditFunc edit_cb;
  float cursor_value = 0.0f;

  /* Filter data. */
  filter = (ANIMFILTER_DATA_VISIBLE | ANIMFILTER_CURVE_VISIBLE | ANIMFILTER_FOREDIT |
            ANIMFILTER_NODUPLIS);
  ANIM_animdata_filter(ac, &anim_data, filter, ac->data, ac->datatype);

  /* Init custom data for iterating over keyframes. */
  memset(&ked, 0, sizeof(KeyframeEditData));
  ked.scene = ac->scene;
  if (mode == GRAPHKEYS_SNAP_NEAREST_MARKER) {
    ked.list.first = (ac->markers) ? ac->markers->first : NULL;
    ked.list.last = (ac->markers) ? ac->markers->last : NULL;
  }
  else if (mode == GRAPHKEYS_SNAP_VALUE) {
    cursor_value = (sipo) ? sipo->cursorVal : 0.0f;
  }
  else if (mode == GRAPHKEYS_SNAP_CFRA) {
    /* In drivers mode, use the cursor value instead
     * (We need to use a different callback for that though)
     */
    if (sipo->mode == SIPO_MODE_DRIVERS) {
      ked.f1 = sipo->cursorTime;
      mode = SNAP_KEYS_TIME;
    }
  }

  /* Get beztriple editing callbacks. */
  edit_cb = ANIM_editkeyframes_snap(mode);

  /* Snap keyframes. */
  for (ale = anim_data.first; ale; ale = ale->next) {
    AnimData *adt = ANIM_nla_mapping_get(ac, ale);

    /* Normalise cursor value (for normalised F-Curves display). */
    if (mode == GRAPHKEYS_SNAP_VALUE) {
      short mapping_flag = ANIM_get_normalization_flags(ac);
      float offset;
      float unit_scale = ANIM_unit_mapping_get_factor(
          ac->scene, ale->id, ale->key_data, mapping_flag, &offset);

      ked.f1 = (cursor_value / unit_scale) - offset;
    }

    /* Perform snapping. */
    if (adt) {
      ANIM_nla_mapping_apply_fcurve(adt, ale->key_data, 0, 0);
      ANIM_fcurve_keyframes_loop(&ked, ale->key_data, NULL, edit_cb, calchandles_fcurve);
      ANIM_nla_mapping_apply_fcurve(adt, ale->key_data, 1, 0);
    }
    else {
      ANIM_fcurve_keyframes_loop(&ked, ale->key_data, NULL, edit_cb, calchandles_fcurve);
    }

    ale->update |= ANIM_UPDATE_DEFAULT;
  }

  ANIM_animdata_update(ac, &anim_data);
  ANIM_animdata_freelist(&anim_data);
}

/* ------------------- */

static int graphkeys_snap_exec(bContext *C, wmOperator *op)
{
  bAnimContext ac;
  short mode;

  /* Get editor data. */
  if (ANIM_animdata_get_context(C, &ac) == 0) {
    return OPERATOR_CANCELLED;
  }

  /* Get snapping mode. */
  mode = RNA_enum_get(op->ptr, "type");

  /* Snap keyframes. */
  snap_graph_keys(&ac, mode);

  /* Set notifier that keyframes have changed. */
  WM_event_add_notifier(C, NC_ANIMATION | ND_KEYFRAME | NA_EDITED, NULL);

  return OPERATOR_FINISHED;
}

void GRAPH_OT_snap(wmOperatorType *ot)
{
  /* Identifiers */
  ot->name = "Snap Keys";
  ot->idname = "GRAPH_OT_snap";
  ot->description = "Snap selected keyframes to the chosen times/values";

  /* API callbacks */
  ot->invoke = WM_menu_invoke;
  ot->exec = graphkeys_snap_exec;
  ot->poll = graphop_editable_keyframes_poll;

  /* Flags */
  ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;

  /* Id-props */
  ot->prop = RNA_def_enum(ot->srna, "type", prop_graphkeys_snap_types, 0, "Type", "");
}

/* ******************** Mirror Keyframes Operator *********************** */

/* Defines for mirror keyframes tool. */
static const EnumPropertyItem prop_graphkeys_mirror_types[] = {
    {GRAPHKEYS_MIRROR_CFRA,
     "CFRA",
     0,
     "By Times Over Current Frame",
     "Flip times of selected keyframes using the current frame as the mirror line"},
    {GRAPHKEYS_MIRROR_VALUE,
     "VALUE",
     0,
     "By Values Over Cursor Value",
     "Flip values of selected keyframes using the cursor value (Y/Horizontal component) as the "
     "mirror line"},
    {GRAPHKEYS_MIRROR_YAXIS,
     "YAXIS",
     0,
     "By Times Over Time=0",
     "Flip times of selected keyframes, effectively reversing the order they appear in"},
    {GRAPHKEYS_MIRROR_XAXIS,
     "XAXIS",
     0,
     "By Values Over Value=0",
     "Flip values of selected keyframes (i.e. negative values become positive, and vice versa)"},
    {GRAPHKEYS_MIRROR_MARKER,
     "MARKER",
     0,
     "By Times Over First Selected Marker",
     "Flip times of selected keyframes using the first selected marker as the reference point"},
    {0, NULL, 0, NULL, NULL},
};

/* This function is responsible for mirroring keyframes. */
static void mirror_graph_keys(bAnimContext *ac, short mode)
{
  ListBase anim_data = {NULL, NULL};
  bAnimListElem *ale;
  int filter;

  SpaceGraph *sipo = (SpaceGraph *)ac->sl;
  KeyframeEditData ked;
  KeyframeEditFunc edit_cb;
  float cursor_value = 0.0f;

  /* Init custom data for looping over keyframes. */
  memset(&ked, 0, sizeof(KeyframeEditData));
  ked.scene = ac->scene;

  /* Store mode-specific custom data... */
  if (mode == GRAPHKEYS_MIRROR_MARKER) {
    TimeMarker *marker = NULL;

    /* Find first selected marker. */
    marker = ED_markers_get_first_selected(ac->markers);

    /* Store marker's time (if available). */
    if (marker) {
      ked.f1 = (float)marker->frame;
    }
    else {
      return;
    }
  }
  else if (mode == GRAPHKEYS_MIRROR_VALUE) {
    cursor_value = (sipo) ? sipo->cursorVal : 0.0f;
  }
  else if (mode == GRAPHKEYS_MIRROR_CFRA) {
    /* In drivers mode, use the cursor value instead
     * (We need to use a different callback for that though)
     */
    if (sipo->mode == SIPO_MODE_DRIVERS) {
      ked.f1 = sipo->cursorTime;
      mode = MIRROR_KEYS_TIME;
    }
  }

  /* Get beztriple editing callbacks. */
  edit_cb = ANIM_editkeyframes_mirror(mode);

  /* Filter data. */
  filter = (ANIMFILTER_DATA_VISIBLE | ANIMFILTER_CURVE_VISIBLE | ANIMFILTER_FOREDIT |
            ANIMFILTER_NODUPLIS);
  ANIM_animdata_filter(ac, &anim_data, filter, ac->data, ac->datatype);

  /* Mirror keyframes. */
  for (ale = anim_data.first; ale; ale = ale->next) {
    AnimData *adt = ANIM_nla_mapping_get(ac, ale);

    /* Apply unit corrections. */
    if (mode == GRAPHKEYS_MIRROR_VALUE) {
      short mapping_flag = ANIM_get_normalization_flags(ac);
      float offset;
      float unit_scale = ANIM_unit_mapping_get_factor(
          ac->scene, ale->id, ale->key_data, mapping_flag | ANIM_UNITCONV_ONLYKEYS, &offset);

      ked.f1 = (cursor_value + offset) * unit_scale;
    }

    /* Perform actual mirroring. */
    if (adt) {
      ANIM_nla_mapping_apply_fcurve(adt, ale->key_data, 0, 0);
      ANIM_fcurve_keyframes_loop(&ked, ale->key_data, NULL, edit_cb, calchandles_fcurve);
      ANIM_nla_mapping_apply_fcurve(adt, ale->key_data, 1, 0);
    }
    else {
      ANIM_fcurve_keyframes_loop(&ked, ale->key_data, NULL, edit_cb, calchandles_fcurve);
    }

    ale->update |= ANIM_UPDATE_DEFAULT;
  }

  ANIM_animdata_update(ac, &anim_data);
  ANIM_animdata_freelist(&anim_data);
}

/* ------------------- */

static int graphkeys_mirror_exec(bContext *C, wmOperator *op)
{
  bAnimContext ac;
  short mode;

  /* Get editor data. */
  if (ANIM_animdata_get_context(C, &ac) == 0) {
    return OPERATOR_CANCELLED;
  }

  /* Get mirroring mode. */
  mode = RNA_enum_get(op->ptr, "type");

  /* Mirror keyframes. */
  mirror_graph_keys(&ac, mode);

  /* Set notifier that keyframes have changed. */
  WM_event_add_notifier(C, NC_ANIMATION | ND_KEYFRAME | NA_EDITED, NULL);

  return OPERATOR_FINISHED;
}

void GRAPH_OT_mirror(wmOperatorType *ot)
{
  /* Identifiers */
  ot->name = "Mirror Keys";
  ot->idname = "GRAPH_OT_mirror";
  ot->description = "Flip selected keyframes over the selected mirror line";

  /* API callbacks */
  ot->invoke = WM_menu_invoke;
  ot->exec = graphkeys_mirror_exec;
  ot->poll = graphop_editable_keyframes_poll;

  /* Flags */
  ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;

  /* Id-props */
  ot->prop = RNA_def_enum(ot->srna, "type", prop_graphkeys_mirror_types, 0, "Type", "");
}

/* ******************** Smooth Keyframes Operator *********************** */

static int graphkeys_smooth_exec(bContext *C, wmOperator *UNUSED(op))
{
  bAnimContext ac;
  ListBase anim_data = {NULL, NULL};
  bAnimListElem *ale;
  int filter;

  /* Get editor data. */
  if (ANIM_animdata_get_context(C, &ac) == 0) {
    return OPERATOR_CANCELLED;
  }

  /* Filter data. */
  filter = (ANIMFILTER_DATA_VISIBLE | ANIMFILTER_CURVE_VISIBLE | ANIMFILTER_FOREDIT |
            ANIMFILTER_NODUPLIS);
  ANIM_animdata_filter(&ac, &anim_data, filter, ac.data, ac.datatype);

  /* Smooth keyframes. */
  for (ale = anim_data.first; ale; ale = ale->next) {
    /* For now, we can only smooth by flattening handles AND smoothing curve values.
     * Perhaps the mode argument could be removed, as that functionality is offered through
     * Snap->Flatten Handles anyway.
     */
    smooth_fcurve(ale->key_data);

    ale->update |= ANIM_UPDATE_DEFAULT;
  }

  ANIM_animdata_update(&ac, &anim_data);
  ANIM_animdata_freelist(&anim_data);

  /* Set notifier that keyframes have changed. */
  WM_event_add_notifier(C, NC_ANIMATION | ND_KEYFRAME | NA_EDITED, NULL);

  return OPERATOR_FINISHED;
}

void GRAPH_OT_smooth(wmOperatorType *ot)
{
  /* Identifiers */
  ot->name = "Smooth Keys";
  ot->idname = "GRAPH_OT_smooth";
  ot->description = "Apply weighted moving means to make selected F-Curves less bumpy";

  /* API callbacks */
  ot->exec = graphkeys_smooth_exec;
  ot->poll = graphop_editable_keyframes_poll;

  /* Flags */
  ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;
}

/* ************************************************************************** */
/* F-CURVE MODIFIERS */

/* ******************** Add F-Modifier Operator *********************** */

static const EnumPropertyItem *graph_fmodifier_itemf(bContext *C,
                                                     PointerRNA *UNUSED(ptr),
                                                     PropertyRNA *UNUSED(prop),
                                                     bool *r_free)
{
  EnumPropertyItem *item = NULL;
  int totitem = 0;
  int i = 0;

  if (C == NULL) {
    return rna_enum_fmodifier_type_items;
  }

  /* Start from 1 to skip the 'Invalid' modifier type. */
  for (i = 1; i < FMODIFIER_NUM_TYPES; i++) {
    const FModifierTypeInfo *fmi = get_fmodifier_typeinfo(i);
    int index;

    /* Check if modifier is valid for this context. */
    if (fmi == NULL) {
      continue;
    }

    index = RNA_enum_from_value(rna_enum_fmodifier_type_items, fmi->type);
    if (index != -1) { /* Not all types are implemented yet... */
      RNA_enum_item_add(&item, &totitem, &rna_enum_fmodifier_type_items[index]);
    }
  }

  RNA_enum_item_end(&item, &totitem);
  *r_free = true;

  return item;
}

static int graph_fmodifier_add_exec(bContext *C, wmOperator *op)
{
  bAnimContext ac;
  ListBase anim_data = {NULL, NULL};
  bAnimListElem *ale;
  int filter;
  short type;

  /* Get editor data. */
  if (ANIM_animdata_get_context(C, &ac) == 0) {
    return OPERATOR_CANCELLED;
  }

  /* Get type of modifier to add. */
  type = RNA_enum_get(op->ptr, "type");

  /* Filter data. */
  filter = (ANIMFILTER_DATA_VISIBLE | ANIMFILTER_FOREDIT | ANIMFILTER_NODUPLIS);
  if (RNA_boolean_get(op->ptr, "only_active")) {
    /* FIXME: enforce in this case only a single channel to get handled? */
    filter |= ANIMFILTER_ACTIVE;
  }
  else {
    filter |= (ANIMFILTER_SEL | ANIMFILTER_CURVE_VISIBLE);
  }
  ANIM_animdata_filter(&ac, &anim_data, filter, ac.data, ac.datatype);

  /* Add f-modifier to each curve. */
  for (ale = anim_data.first; ale; ale = ale->next) {
    FCurve *fcu = (FCurve *)ale->data;
    FModifier *fcm;

    /* Add F-Modifier of specified type to active F-Curve, and make it the active one. */
    fcm = add_fmodifier(&fcu->modifiers, type, fcu);
    if (fcm) {
      set_active_fmodifier(&fcu->modifiers, fcm);
    }
    else {
      BKE_report(op->reports, RPT_ERROR, "Modifier could not be added (see console for details)");
      break;
    }

    ale->update |= ANIM_UPDATE_DEPS;
  }

  ANIM_animdata_update(&ac, &anim_data);
  ANIM_animdata_freelist(&anim_data);

  /* Set notifier that things have changed. */
  WM_event_add_notifier(C, NC_ANIMATION | ND_KEYFRAME | NA_EDITED, NULL);

  return OPERATOR_FINISHED;
}

void GRAPH_OT_fmodifier_add(wmOperatorType *ot)
{
  PropertyRNA *prop;

  /* Identifiers */
  ot->name = "Add F-Curve Modifier";
  ot->idname = "GRAPH_OT_fmodifier_add";
  ot->description = "Add F-Modifier to the active/selected F-Curves";

  /* API callbacks */
  ot->invoke = WM_menu_invoke;
  ot->exec = graph_fmodifier_add_exec;
  ot->poll = graphop_selected_fcurve_poll;

  /* Flags */
  ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;

  /* Id-props */
  prop = RNA_def_enum(ot->srna, "type", rna_enum_fmodifier_type_items, 0, "Type", "");
  RNA_def_property_translation_context(prop, BLT_I18NCONTEXT_ID_ACTION);
  RNA_def_enum_funcs(prop, graph_fmodifier_itemf);
  ot->prop = prop;

  RNA_def_boolean(
      ot->srna, "only_active", 1, "Only Active", "Only add F-Modifier to active F-Curve");
}

/* ******************** Copy F-Modifiers Operator *********************** */

static int graph_fmodifier_copy_exec(bContext *C, wmOperator *op)
{
  bAnimContext ac;
  bAnimListElem *ale;
  bool ok = false;

  /* Get editor data. */
  if (ANIM_animdata_get_context(C, &ac) == 0) {
    return OPERATOR_CANCELLED;
  }

  /* Clear buffer first. */
  ANIM_fmodifiers_copybuf_free();

  /* Get the active F-Curve. */
  ale = get_active_fcurve_channel(&ac);

  /* If this exists, call the copy F-Modifiers API function. */
  if (ale && ale->data) {
    FCurve *fcu = (FCurve *)ale->data;

    /* TODO: When 'active' vs 'all' boolean is added, change last param! (Joshua Leung 2010) */
    ok = ANIM_fmodifiers_copy_to_buf(&fcu->modifiers, 0);

    /* Free temp data now. */
    MEM_freeN(ale);
  }

  /* Successful or not? */
  if (ok == 0) {
    BKE_report(op->reports, RPT_ERROR, "No F-Modifiers available to be copied");
    return OPERATOR_CANCELLED;
  }
  return OPERATOR_FINISHED;
}

void GRAPH_OT_fmodifier_copy(wmOperatorType *ot)
{
  /* Identifiers */
  ot->name = "Copy F-Modifiers";
  ot->idname = "GRAPH_OT_fmodifier_copy";
  ot->description = "Copy the F-Modifier(s) of the active F-Curve";

  /* API callbacks */
  ot->exec = graph_fmodifier_copy_exec;
  ot->poll = graphop_active_fcurve_poll;

  /* Flags */
  ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;

  /* Id-props */
#if 0
  ot->prop = RNA_def_boolean(ot->srna,
                             "all",
                             1,
                             "All F-Modifiers",
                             "Copy all the F-Modifiers, instead of just the active one");
#endif
}

/* ******************** Paste F-Modifiers Operator *********************** */

static int graph_fmodifier_paste_exec(bContext *C, wmOperator *op)
{
  bAnimContext ac;

  ListBase anim_data = {NULL, NULL};
  bAnimListElem *ale;
  int filter;

  const bool replace = RNA_boolean_get(op->ptr, "replace");
  bool ok = false;

  /* Get editor data. */
  if (ANIM_animdata_get_context(C, &ac) == 0) {
    return OPERATOR_CANCELLED;
  }

  /* Filter data. */
  if (RNA_boolean_get(op->ptr, "only_active")) {
    /* This should be the default (for buttons) - Just paste to the active FCurve. */
    filter = (ANIMFILTER_DATA_VISIBLE | ANIMFILTER_ACTIVE | ANIMFILTER_FOREDIT |
              ANIMFILTER_NODUPLIS);
  }
  else {
    /* This is only if the operator gets called from a hotkey or search -
     * Paste to all visible curves. */
    filter = (ANIMFILTER_DATA_VISIBLE | ANIMFILTER_CURVE_VISIBLE | ANIMFILTER_SEL |
              ANIMFILTER_FOREDIT | ANIMFILTER_NODUPLIS);
  }

  ANIM_animdata_filter(&ac, &anim_data, filter, ac.data, ac.datatype);

  /* Paste modifiers. */
  for (ale = anim_data.first; ale; ale = ale->next) {
    FCurve *fcu = (FCurve *)ale->data;
    int tot;

    tot = ANIM_fmodifiers_paste_from_buf(&fcu->modifiers, replace, fcu);

    if (tot) {
      ale->update |= ANIM_UPDATE_DEPS;
      ok = true;
    }
  }

  if (ok) {
    ANIM_animdata_update(&ac, &anim_data);
  }
  ANIM_animdata_freelist(&anim_data);

  /* Successful or not?. */
  if (ok) {
    /* Set notifier that keyframes have changed. */
    WM_event_add_notifier(C, NC_ANIMATION | ND_KEYFRAME | NA_EDITED, NULL);

    return OPERATOR_FINISHED;
  }

  BKE_report(op->reports, RPT_ERROR, "No F-Modifiers to paste");
  return OPERATOR_CANCELLED;
}

void GRAPH_OT_fmodifier_paste(wmOperatorType *ot)
{
  /* Identifiers */
  ot->name = "Paste F-Modifiers";
  ot->idname = "GRAPH_OT_fmodifier_paste";
  ot->description = "Add copied F-Modifiers to the selected F-Curves";

  /* API callbacks */
  ot->exec = graph_fmodifier_paste_exec;
  ot->poll = graphop_active_fcurve_poll;

  /* Flags */
  ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;

  /* Properties */
  RNA_def_boolean(
      ot->srna, "only_active", false, "Only Active", "Only paste F-Modifiers on active F-Curve");
  RNA_def_boolean(
      ot->srna,
      "replace",
      false,
      "Replace Existing",
      "Replace existing F-Modifiers, instead of just appending to the end of the existing list");
}

/* ************************************************************************** */
/* Drivers */

/* ******************** Copy Driver Vars Operator *********************** */

static int graph_driver_vars_copy_exec(bContext *C, wmOperator *op)
{
  bool ok = false;

  PointerRNA ptr = CTX_data_pointer_get_type(C, "active_editable_fcurve", &RNA_FCurve);

  /* If this exists, call the copy driver vars API function. */
  FCurve *fcu = ptr.data;

  if (fcu) {
    ok = ANIM_driver_vars_copy(op->reports, fcu);
  }

  /* Successful or not?. */
  if (ok) {
    return OPERATOR_FINISHED;
  }
  return OPERATOR_CANCELLED;
}

void GRAPH_OT_driver_variables_copy(wmOperatorType *ot)
{
  /* Identifiers */
  ot->name = "Copy Driver Variables";
  ot->idname = "GRAPH_OT_driver_variables_copy";
  ot->description = "Copy the driver variables of the active driver";

  /* API callbacks */
  ot->exec = graph_driver_vars_copy_exec;
  ot->poll = graphop_active_editable_fcurve_ctx_poll;

  /* Flags */
  ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;
}

/* ******************** Paste Driver Vars Operator *********************** */

static int graph_driver_vars_paste_exec(bContext *C, wmOperator *op)
{
  const bool replace = RNA_boolean_get(op->ptr, "replace");
  bool ok = false;

  PointerRNA ptr = CTX_data_pointer_get_type(C, "active_editable_fcurve", &RNA_FCurve);

  /* If this exists, call the paste driver vars API function. */
  FCurve *fcu = ptr.data;

  if (fcu) {
    ok = ANIM_driver_vars_paste(op->reports, fcu, replace);
  }

  /* Successful or not?. */
  if (ok) {
    /* Rebuild depsgraph, now that there are extra deps here. */
    DEG_relations_tag_update(CTX_data_main(C));

    /* Set notifier that keyframes have changed. */
    WM_event_add_notifier(C, NC_SCENE | ND_FRAME, CTX_data_scene(C));

    return OPERATOR_FINISHED;
  }
  return OPERATOR_CANCELLED;
}

void GRAPH_OT_driver_variables_paste(wmOperatorType *ot)
{
  /* Identifiers */
  ot->name = "Paste Driver Variables";
  ot->idname = "GRAPH_OT_driver_variables_paste";
  ot->description = "Add copied driver variables to the active driver";

  /* API callbacks */
  ot->exec = graph_driver_vars_paste_exec;
  ot->poll = graphop_active_editable_fcurve_ctx_poll;

  /* Flags */
  ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;

  /* Properties */
  RNA_def_boolean(ot->srna,
                  "replace",
                  false,
                  "Replace Existing",
                  "Replace existing driver variables, instead of just appending to the end of the "
                  "existing list");
}

/* ************************************************************************** */

static int graph_driver_delete_invalid_exec(bContext *C, wmOperator *op)
{
  bAnimContext ac;
  ListBase anim_data = {NULL, NULL};
  bAnimListElem *ale;
  int filter;
  bool ok = false;
  uint deleted = 0;

  /* Get editor data. */
  if (ANIM_animdata_get_context(C, &ac) == 0) {
    return OPERATOR_CANCELLED;
  }

  /* NOTE: We might need a scene update to evaluate the driver flags. */

  /* Filter data. */
  filter = (ANIMFILTER_DATA_VISIBLE | ANIMFILTER_CURVE_VISIBLE | ANIMFILTER_NODUPLIS);
  ANIM_animdata_filter(&ac, &anim_data, filter, ac.data, ac.datatype);

  /* Find invalid drivers. */
  for (ale = anim_data.first; ale; ale = ale->next) {
    FCurve *fcu = (FCurve *)ale->data;
    if (ELEM(NULL, fcu, fcu->driver)) {
      continue;
    }
    if (!(fcu->driver->flag & DRIVER_FLAG_INVALID)) {
      continue;
    }

    ok |= ANIM_remove_driver(op->reports, ale->id, fcu->rna_path, fcu->array_index, 0);
    if (!ok) {
      break;
    }
    deleted += 1;
  }

  /* Cleanup. */
  ANIM_animdata_freelist(&anim_data);

  if (deleted > 0) {
    /* Notify the world of any changes. */
    DEG_relations_tag_update(CTX_data_main(C));
    WM_event_add_notifier(C, NC_ANIMATION | ND_KEYFRAME | NA_REMOVED, NULL);
    WM_reportf(RPT_INFO, "Deleted %u drivers", deleted);
  }
  else {
    WM_report(RPT_INFO, "No drivers deleted");
  }

  /* Successful or not?*/
  if (!ok) {
    return OPERATOR_CANCELLED;
  }

  return OPERATOR_FINISHED;
}

static bool graph_driver_delete_invalid_poll(bContext *C)
{
  bAnimContext ac;
  ScrArea *area = CTX_wm_area(C);

  /* Firstly, check if in Graph Editor. */
  if ((area == NULL) || (area->spacetype != SPACE_GRAPH)) {
    return false;
  }

  /* Try to init Anim-Context stuff ourselves and check. */
  return ANIM_animdata_get_context(C, &ac) != 0;
}

void GRAPH_OT_driver_delete_invalid(wmOperatorType *ot)
{
  /* Identifiers */
  ot->name = "Delete Invalid Drivers";
  ot->idname = "GRAPH_OT_driver_delete_invalid";
  ot->description = "Delete all visible drivers considered invalid";

  /* API callbacks */
  ot->exec = graph_driver_delete_invalid_exec;
  ot->poll = graph_driver_delete_invalid_poll;

  /* Flags */
  ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;
}
