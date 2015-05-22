/*******************************************************************************
 * Copyright (c) 2007, 2015 IBM Corporation and others.
 * All rights reserved. This program and the accompanying materials
 * are made available under the terms of the Eclipse Public License v1.0
 * which accompanies this distribution, and is available at 
 * http://www.eclipse.org/legal/epl-v10.html
 * 
 * Contributors:
 *     IBM Corporation - initial API and implementation
 *******************************************************************************/

#include "eclipseGtk.h"
#include "eclipseCommon.h"
#include <dlfcn.h>
#include <string.h>
#include <stdlib.h>

struct GTK_PTRS gtk = { 1 }; /* initialize the first field "not_initialized" so we can tell when we've loaded the pointers */

static _TCHAR* minVerMsg1 = _T_ECLIPSE("Starting from the Eclipse 4.5 (Mars) release, \nGTK+ versions below");
static _TCHAR* minVerMsg2 = _T_ECLIPSE("are not supported.\nGTK+ version found is");
static _TCHAR* minVerTitle = _T_ECLIPSE("Unsupported GTK+ version");
static _TCHAR* gtkInitFail = _T_ECLIPSE("Unable to initialize GTK+\n");
static _TCHAR* upgradeWarning1 = _T_ECLIPSE("\nPlease upgrade GTK+ to minimum version");
static _TCHAR* upgradeWarning2 = _T_ECLIPSE("\nor use an older version of Eclipse.\nClick OK to Exit.");
static int minGtkMajorVersion = 2;
static int minGtkMinorVersion = 18;
static int minGtkMicroVersion = 0;
#ifdef __ppc64le__
static int minGtk3MajorVersion = 3;
static int minGtk3MinorVersion = 10;
static int minGtk3MicroVersion = 9;
#endif

/* tables to help initialize the function pointers */
/* functions from libgtk-x11-2.0 or libgtk-3.so.0*/
static FN_TABLE gtkFunctions[] = {
	FN_TABLE_ENTRY(gtk_container_add, 1),
	FN_TABLE_ENTRY(gtk_dialog_run, 1),
	FN_TABLE_ENTRY(gtk_image_new_from_pixbuf, 1),
	FN_TABLE_ENTRY(gtk_init_check, 1),
	FN_TABLE_ENTRY(gtk_init_with_args, 0),
	FN_TABLE_ENTRY(gtk_message_dialog_new, 1),
	FN_TABLE_ENTRY(gtk_set_locale, 0),
	FN_TABLE_ENTRY(gtk_widget_destroy, 1),
	FN_TABLE_ENTRY(gtk_widget_destroyed, 1),
	FN_TABLE_ENTRY(gtk_widget_show_all, 1),
	FN_TABLE_ENTRY(gtk_window_new, 1),
	FN_TABLE_ENTRY(gtk_window_resize, 1),
	FN_TABLE_ENTRY(gtk_window_set_title, 1),
	FN_TABLE_ENTRY(gtk_window_set_decorated, 1),
	FN_TABLE_ENTRY(gtk_window_set_type_hint, 1),
	FN_TABLE_ENTRY(gtk_window_set_position, 1),
	{ NULL, NULL }
};
/* functions from libgdk-x11-2.0 or libgdk-3.so.0*/
static FN_TABLE gdkFunctions[] = {
	FN_TABLE_ENTRY(gdk_set_program_class, 1),
	FN_TABLE_ENTRY(gdk_display_get_default, 1),
	FN_TABLE_ENTRY(gdk_x11_display_get_xdisplay, 1),
	{ NULL, NULL }
};
/* functions from libgdk_pixbuf-2.0 */
static FN_TABLE pixFunctions[] = {
	FN_TABLE_ENTRY(gdk_pixbuf_new_from_file, 1),
	FN_TABLE_ENTRY(gdk_pixbuf_get_width, 1),
	FN_TABLE_ENTRY(gdk_pixbuf_get_height, 1),
	{ NULL, NULL }
};
/* functions from libgobject-2.0 */
static FN_TABLE gobjFunctions[] = {
	FN_TABLE_ENTRY(g_signal_connect_data, 1),
	FN_TABLE_ENTRY(g_main_context_iteration, 1),
	FN_TABLE_ENTRY(g_object_unref, 1),
	FN_TABLE_ENTRY(g_timeout_add, 1),
	FN_TABLE_ENTRY(g_error_free, 1),
#ifdef SOLARIS
	FN_TABLE_ENTRY(g_string_insert_c, 1),
#endif
	{ NULL, NULL }
};

/* functions from libX11 */
static FN_TABLE x11Functions[] = {
	FN_TABLE_ENTRY(XGetSelectionOwner, 1),
	FN_TABLE_ENTRY(XSetSelectionOwner, 1),
	FN_TABLE_ENTRY(XCreateWindow, 1),
	FN_TABLE_ENTRY(XChangeProperty, 1),
	FN_TABLE_ENTRY(XSync, 1),
	FN_TABLE_ENTRY(XRootWindow, 1),
	FN_TABLE_ENTRY(XDefaultScreen, 1),
	FN_TABLE_ENTRY(XInternAtom, 1),
	{ NULL, NULL }
};

static int loadGtkSymbols( void * library, FN_TABLE * table) {
	int i = 0;
	void * fn;
	for (i = 0; table[i].fnName != NULL; i++) {
		fn = findSymbol(library, table[i].fnName);
		if (fn != 0) {
			*(table[i].fnPtr) = fn;
		} else {
			if (table[i].required) return -1;
		}
	}
	return 0;
}

int loadGtk() {
#ifdef AIX 
#define DLFLAGS RTLD_LAZY | RTLD_MEMBER
#else
#define DLFLAGS RTLD_LAZY
#endif

	char *overlayScrollbar = getenv("LIBOVERLAY_SCROLLBAR");
	if (overlayScrollbar == NULL) {
		setenv("LIBOVERLAY_SCROLLBAR", "0", 0);
	}

	char *oxygenGtkHack = getenv("OXYGEN_DISABLE_INNER_SHADOWS_HACK");
	if (oxygenGtkHack == NULL) {
		setenv("OXYGEN_DISABLE_INNER_SHADOWS_HACK", "1", 0);
	}

	void *gdkLib = NULL, *gtkLib = NULL, *objLib = NULL, *pixLib = NULL, *x11Lib = NULL;
	
	char *gtk3 = getenv("SWT_GTK3");
	if (gtk3 == NULL || strcmp(gtk3,"1") == 0) {
		gdkLib = dlopen(GDK3_LIB, DLFLAGS);
		gtkLib = dlopen(GTK3_LIB, DLFLAGS);

		/* Work around for https://bugzilla.gnome.org/show_bug.cgi?id=677329, see Eclipse bug 435742 */
		char *gdkCoreDeviceEvents = getenv("GDK_CORE_DEVICE_EVENTS");
		if (gdkCoreDeviceEvents == NULL) {
			setenv("GDK_CORE_DEVICE_EVENTS", "1", 0);
		}
#ifdef __ppc64le__
		if (gtkLib != NULL) {
			const char * (*func)(int, int, int);
			dlerror();
			*(void**) (&func) = dlsym(gtkLib, "gtk_check_version");
			if (dlerror() == NULL && func) {
				const char *check = (*func)(minGtk3MajorVersion, minGtk3MinorVersion, minGtk3MicroVersion);
				if (check != NULL) {
					dlclose(gdkLib);
					dlclose(gtkLib);
					gdkLib = gtkLib = NULL;
					setenv("SWT_GTK3","0",0);
				}
			}
		}
#endif

	}
	if (!gtkLib || !gdkLib) { //if GTK+ 2
		gdkLib = dlopen(GDK_LIB, DLFLAGS);
		gtkLib = dlopen(GTK_LIB, DLFLAGS);
		setenv("SWT_GTK3","0",1);

		const char * (*func)(int, int, int);
		dlerror();

		*(void**) (&func) = dlsym(gtkLib, "gtk_check_version");
		if (dlerror() == NULL && func) {
			const char *check = (*func)(minGtkMajorVersion, minGtkMinorVersion, minGtkMicroVersion);
			if (check != NULL) {
				GtkWidget* dialog;
				int gtkMajorVersion, gtkMinorVersion, gtkMicroVersion;
				void *gtkMajorPtr, *gtkMinorPtr, *gtkMicroPtr;

				/* this code is applicable for GTK+ 2 only*/
				dlerror();
				gtkMajorPtr = dlsym(gtkLib, "gtk_major_version");
				if ((dlerror() != NULL) || (gtkMajorPtr == NULL)) return -1;
				gtkMajorVersion = *(int *)gtkMajorPtr;

				gtkMinorPtr = dlsym(gtkLib, "gtk_minor_version");
				if ((dlerror() != NULL) || (gtkMinorPtr == NULL)) return -1;
				gtkMinorVersion = *(int *)gtkMinorPtr;

				gtkMicroPtr = dlsym(gtkLib, "gtk_micro_version");
				if ((dlerror() != NULL) || (gtkMicroPtr == NULL)) return -1;
				gtkMicroVersion = *(int *)gtkMicroPtr;

				objLib = dlopen(GOBJ_LIB, DLFLAGS);
				pixLib = dlopen(PIXBUF_LIB, DLFLAGS);
				x11Lib = dlopen(X11_LIB, DLFLAGS);

				memset(&gtk, 0, sizeof(struct GTK_PTRS));

				if ( gtkLib == NULL || loadGtkSymbols(gtkLib, gtkFunctions)  != 0) return -1;
				if ( gdkLib == NULL || loadGtkSymbols(gdkLib, gdkFunctions)  != 0) return -1;
				if ( pixLib == NULL || loadGtkSymbols(pixLib, pixFunctions)  != 0) return -1;
				if ( objLib == NULL || loadGtkSymbols(objLib, gobjFunctions) != 0) return -1;
				if ( x11Lib == NULL || loadGtkSymbols(x11Lib, x11Functions)  != 0) return -1;

				/* Initialize GTK. */
				if (gtk.gtk_set_locale) gtk.gtk_set_locale();
				if (gtk.gtk_init_with_args) {
					GError *error = NULL;
					if (!gtk.gtk_init_with_args(0, NULL, NULL, NULL, NULL, &error)) {
						printf("%s", gtkInitFail);
						exit (1);
					}
				}
				dialog = gtk.gtk_message_dialog_new(NULL, GTK_DIALOG_DESTROY_WITH_PARENT,
						GTK_MESSAGE_ERROR, GTK_BUTTONS_OK,
						"%s %d.%d.%d %s %d.%d.%d.\n%s %d.%d.%d %s", minVerMsg1, minGtkMajorVersion,
						minGtkMinorVersion, minGtkMicroVersion, minVerMsg2, gtkMajorVersion,
						gtkMinorVersion, gtkMicroVersion, upgradeWarning1, minGtkMajorVersion,
						minGtkMinorVersion, minGtkMicroVersion, upgradeWarning2);
				gtk.gtk_window_set_title((GtkWindow*)dialog, minVerTitle);
				gtk.gtk_dialog_run((GtkDialog*)dialog);
				gtk.gtk_widget_destroy(dialog);
				dlclose(gdkLib);
				dlclose(gtkLib);
				gdkLib = gtkLib = NULL;
				exit (1);
			}
		}
	}


	objLib = dlopen(GOBJ_LIB, DLFLAGS);
	pixLib = dlopen(PIXBUF_LIB, DLFLAGS);
	x11Lib = dlopen(X11_LIB, DLFLAGS);

	/* initialize ptr struct to 0's */
	memset(&gtk, 0, sizeof(struct GTK_PTRS));

	if ( gtkLib == NULL || loadGtkSymbols(gtkLib, gtkFunctions)  != 0) return -1;
	if ( gdkLib == NULL || loadGtkSymbols(gdkLib, gdkFunctions)  != 0) return -1;
	if ( pixLib == NULL || loadGtkSymbols(pixLib, pixFunctions)  != 0) return -1;
	if ( objLib == NULL || loadGtkSymbols(objLib, gobjFunctions) != 0) return -1;
	if ( x11Lib == NULL || loadGtkSymbols(x11Lib, x11Functions) != 0) return -1;

	return 0;
}
