// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 the NXAevora contributors
// The pure layout and field edit rules of the engine menu. No ImGui, so the
// layout test links this file alone.

#pragma once

#include <stddef.h>

namespace md3 {

// Decision 2 of the display layout brief: the width that a row's label and
// supporting text may use. Each size is in pixels at the current theme scale.
struct TextColumnInput {
    float row_width;
    float row_padding; // on each side
    float icon_slot;   // leading restart icon slot, 0 when there is none
    float right_width; // the right part
    float gap;         // between the text and the right part
};

// The row width minus the two row paddings, the icon slot, the right part and
// the gap, never below 0. A negative or not finite size counts as 0.
float TextColumnWidth(const TextColumnInput& in);

// Decision 1: the row height from the measured text block.
struct RowHeightInput {
    float text_height;      // label + gap + wrapped supporting text
    float vertical_padding; // above and below the text
    float min_height;       // the one-line or two-line row metric
    float right_height;     // the right part
};

// The larger of min_height, right_height and the text block with its two
// paddings. A negative or not finite size counts as 0.
float SettingsRowHeight(const RowHeightInput& in);

// Decision 5: the page scroll that shows the row of the focused nav item.
struct PageScrollInput {
    float row_top;     // in the page's content coordinates
    float row_bottom;  // in the page's content coordinates
    float view_height; // the visible height of the page
    float current_scroll;
    float max_scroll;
    bool first_row; // the first nav item of the page
    bool last_row;  // the last nav item of the page
};

// The first row gives 0 and the last row gives the maximum. Else a row above
// the view scrolls to its top, a row below the view to its bottom, and a row
// already visible keeps the scroll. The result is clamped to 0..max_scroll.
float PageScrollTarget(const PageScrollInput& in);

// The most text that a text field keeps, and the size of its stored line.
inline constexpr size_t kTextFieldMax = 1024;

// The line of a field that lost the edit state to another field, kept for the
// frame of the take-over and the next one.
struct FieldLeave {
    unsigned int id; // the field that left, 0 when there is none
    int frame;       // the frame of the take-over
    char text[kTextFieldMax];
};

// The edit state of the number and text fields. One item is active at a time,
// so one state serves every field: it belongs to the field that started the
// edit and keeps that field's line and error after the edit ends. A field whose
// id is not the id here is not editing and has no error. The leaving entry
// keeps the line of a field that a take-over replaced before it could commit.
struct FieldEdit {
    unsigned int id; // the owner field id, 0 when there is none
    int frame;       // the frame of the owner's last draw
    char text[kTextFieldMax];
    bool editing;
    bool error;
    FieldLeave leave;
};

// The draw start of field id on frame. An edit of the owner that the page did
// not draw in the last frame ends, and then id records this frame when it owns
// the state. Returns true when id owns the state.
bool FieldEditBegin(FieldEdit& state, unsigned int id, int frame);

// True when id owns the state.
bool FieldEditOwnedBy(const FieldEdit& state, unsigned int id);

// True while the owner edits and drew in this frame or the frame before, so the
// text keys stay with it and a commit of the deactivated item can land.
bool FieldEditEditing(const FieldEdit& state, int frame);

// Starts an edit of field id with line text and no error. The field takes the
// state from an other owner that edits into the leaving entry, because only one
// item is active; with no such owner the leaving entry is cleared.
void FieldEditStart(FieldEdit& state, unsigned int id, int frame, const char* text);

// Copies the leaving line of id into line and clears the entry. Returns false
// when id is not the leaving field or the entry is older than the next frame.
bool FieldEditTakeLeaving(FieldEdit& state, unsigned int id, int frame, char* line, size_t size);

// Ends the owner's edit and sets error. The owner id stays, so the error stays
// with the field until another field starts an edit.
void FieldEditEnd(FieldEdit& state, bool error);

// Ends the owner's edit without error and clears the leaving entry, for a menu
// close that cancels the edits.
void FieldEditCancel(FieldEdit& state);

} // namespace md3
