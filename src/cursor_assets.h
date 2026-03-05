#pragma once

#include <cstddef>

/** Single embedded cursor image (PNG bytes). */
struct CursorAsset {
	const char *filename;   /**< e.g. "pointer.png" */
	const unsigned char *data;
	size_t size;
};

/** Number of embedded cursor assets. */
int get_cursor_asset_count();

/** All embedded cursor assets (array length = get_cursor_asset_count()). */
const CursorAsset *get_cursor_assets();

/** Find by filename (e.g. "pointer.png"), or nullptr if not found. */
const CursorAsset *get_cursor_asset_by_name(const char *filename);
