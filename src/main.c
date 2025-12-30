/*
ThinkPad Fan Control
Copyright 2022, Elias Van Ootegem <elias@vega.xyz>

This is a reworked version of the original ThinkPad fan control 
Copyright 2008, Stanko Tadić <stanko@mfhinc.net>
The GUI was made from scratch for GTK 3.24, closely modeled on the original (at least for now)
Some new controls were added (most notably, a control to adjust the scan interval when switching to manual fan control).
There are more QOL improvements and tweaks planned, though, and we might abandon the original design all together in the future.
The license remains unchanged:

ThinkPad fan control is free software; you can redistribute it 
and/or modify it under the terms of the GNU General Public License 
version 2 as published by the Free Software Foundation.  
Note that I am not granting permission to redistribute or 
modify ThinkPad fan control under the terms of any later version of the 
General Public License.

ThinkPad fan control is distributed in the hope that it will be useful, but WITHOUT
ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
for more details.

You should have received a copy of the GNU General Public License
along with this program (in the file "LICENCE"); if not, write to the
Free Software Foundation, Inc., 59 Temple Place - Suite 330, Boston,
MA 02111, USA.
*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdbool.h>
#include <gtk/gtk.h>
#include <ctype.h>

#define GTK_GUI_FILE "src/gui_new.glade"
#define FAN_LVL_AUTO 0
#define FAN_LVL_FULL 9
// scan interval, critical temp, safe temp, fan speed
#define AUTO_LBL_FMT "Current Options: %ds - %dC - %dC - %s"

const char *fan_speeds[] = {
    "Auto",
    "0",
    "1",
    "2",
    "3",
    "4",
    "5",
    "6",
    "7",
    "Full-Speed",
};

// The widgets and values we need, slightly better organised in terms of per-control/task
/*
typedef struct _window_visible {
    GtkWidget *window;
    GtkStatusbar *status_bar;
    int visible;
} window_visible;

typedef struct _speed_changes {
    GtkWidget *window;
    GtkStatusbar *status_bar;
    GtkLabel *current_lbl;
    GtkLabel *auto_lbl;
    GtkComboBox *auto_cmb;
    GtkComboBox *man_cmb;
    GtkSpinButton *auto_int, *man_int, *crit, *safe;
    int manual, running, was_crit; // 0 for auto, running indicates current timeout running
    gint timeout;
    // config for auto, so we don't have to get it from widgets all the time
    int temp_safe, temp_crit, scan_interval, fan_speed;
} speed_changes;
*/

typedef struct _fan_curve {
    GtkLabel *config;
    GtkComboBox *safe_cmb, *crit_cmb, *inc_cmb;
    GtkSpinButton *safe, *crit, *scan_int, *delta;
    GtkScale *throttle_scl;
    int step, safe_temp, crit_temp, delta_temp, scan, safe_speed, crit_speed; // last values from input
    int fan_speed; // current fan speed
    double throttle_factor;
} fan_curve;

typedef struct _application {
    // general widgets we can use
    GtkWidget *window;
    GtkStatusbar *status_bar;
    GtkNotebook *main_nb;
    GtkDialog *close;
    // @TODO this is deprecated
    GtkStatusIcon *tray_icon;
    // this is only used for temps/speed, but is a general label show everywhere
    GtkLabel *current_lbl, *close_lbl;
    // speed specific widgets
    GtkLabel *auto_lbl;
    GtkComboBox *auto_cmb;
    GtkComboBox *man_cmb;
    GtkSpinButton *auto_int, *man_int, *crit, *safe;
    GtkButton *off_btn;
    // everything for the fan curve in its own type
    fan_curve *curve;
    // manual vs auto mode, was critical Y/N
    int manual, running, was_crit; // 0 for auto, running indicates current timeout running
    // speed/temp interval callback thing
    gint timeout, status_id;
    // config for auto, used to not access widgets all the time + check if anything changed
    int temp_safe, temp_crit, scan_interval, fan_speed;
    // used for minimization
    int visible; // is for minimization
    // chatty or not
    bool print_logs;
} application;

// declare some funcs that are in the wrong place
int update_temps(gpointer data);
void change_fan_speed(int new_speed, application *app);

// function to execute when we absolutely, for sure, unequivocally are shutting down
void exit_app(application *app) {
    if (app->running) {
        g_source_remove(app->timeout);
    }
    if (app->print_logs)
        printf("Exit\n");
    gtk_main_quit();
}

int get_cpu_temp(application *app) {
    int temp = 0;
    FILE *temp_input;
    // get current CPU temp
    temp_input = fopen("/proc/acpi/ibm/thermal","r");
    if(temp_input == NULL){
        gtk_label_set_text(app->current_lbl, "YOU ARE NOT RUNNING KERNEL WITH THINKPAD PATCH!");
        return -1;
    }
    fscanf(temp_input, "temperatures:	%d", &temp);
    fclose(temp_input);
    return temp;
}

int set_curve_values(fan_curve *curve) {
    char tmp_string[250];
    int temp_crit, temp_safe, delta, f_step;
    gdouble throttle_val;
    temp_crit = gtk_spin_button_get_value_as_int(curve->crit);
    temp_safe = gtk_spin_button_get_value_as_int(curve->safe);
    delta = gtk_spin_button_get_value_as_int(curve->delta);
    throttle_val = gtk_range_get_value(GTK_RANGE(curve->throttle_scl));
    if (temp_safe >= temp_crit || (temp_safe + delta) >= temp_crit) {
        gtk_label_set_text(curve->config, "Save temperature must be < critical temperature - delta");
        return 0;
    }
    curve->safe_temp = temp_safe;
    curve->crit_temp = temp_crit;
    curve->delta_temp = delta;
    curve->scan = gtk_spin_button_get_value_as_int(curve->scan_int);
    curve->safe_speed = gtk_combo_box_get_active(curve->safe_cmb);
    curve->crit_speed = gtk_combo_box_get_active(curve->crit_cmb);
    curve->throttle_factor = throttle_val/100;
    curve->step = gtk_combo_box_get_active(curve->inc_cmb) + 1; // @TODO check for 0
    f_step = (int) ((double) delta * curve->throttle_factor);
    // ok, we have some settings, let's write it out
    sprintf(tmp_string,
            "Fan speed @ safe temp %d: %s\nFan speed step %d per %d degrees\nFan speed @ critical temp %d: %s\nScan every %ds\nThrottle factor: %.2f (%d)",
            temp_safe, fan_speeds[curve->safe_speed], curve->step, curve->delta_temp, temp_crit, fan_speeds[curve->crit_speed], curve->scan, curve->throttle_factor, f_step);
    gtk_label_set_text(curve->config, tmp_string);
    return 1;
}

int set_auto_values(application *data) {
    char tmp_string[250];
    int temp_crit, temp_safe;
    temp_crit = gtk_spin_button_get_value_as_int(data->crit);
    temp_safe = gtk_spin_button_get_value_as_int(data->safe);
    // check input
    if (temp_safe >= temp_crit) {
        gtk_label_set_text(data->auto_lbl, "Safe temperature must be < critical temperature");
        return 0;
    }
    // update profile now that we know the temps make sense
    data->temp_crit = temp_crit;
    data->temp_safe = temp_safe;
    data->fan_speed = gtk_combo_box_get_active(data->auto_cmb);
    data->scan_interval = gtk_spin_button_get_value_as_int(data->auto_int);
    sprintf(
        tmp_string,
        "Current options: Safe: %d, Critical: %d, Scan interval: %d\nFan speed when critical: %s",
        data->temp_safe,
        data->temp_crit,
        data->scan_interval,
        fan_speeds[data->fan_speed]
    );
    gtk_label_set_text(data->auto_lbl, tmp_string);
    if (!data->running) {
        gtk_label_set_text(data->current_lbl, "Click apply to run with these settings");
    } else {
        gtk_label_set_text(data->current_lbl, "Applying settings...");
    }
    return 1;
}

void set_manual_values(application *data) {
    data->fan_speed = gtk_combo_box_get_active(data->man_cmb);
    data->scan_interval = gtk_spin_button_get_value_as_int(data->man_int);
}

// close application
void window_destroy(GtkWidget *object, gpointer data) {
    char tmp_str[100];
    application *app = data;
    // remove timeout if running
    if (app->running) {
        if (app->fan_speed == 0) {
            // fan speed is already in auto, we don't need to prompt to set it to auto, just silently exit
            if (app->print_logs)
                printf("Fan already in auto, timout removed, exit\n");
            exit_app(app);
            return;
        }
        // show close dialog
        sprintf(tmp_str, "Current fan speed: %s (%d)", fan_speeds[app->fan_speed], app->fan_speed);
        gtk_label_set_text(app->close_lbl, tmp_str);
        gtk_widget_show(GTK_WIDGET(app->close));
        return;
    }
    exit_app(app);
}

// minimize -> have taskbar icon
void hide_window(GtkStatusIcon *status_icon, gpointer user_data) {
    application *app= user_data;
    if (app->visible) {
        gtk_widget_hide(GTK_WIDGET(app->window));
        app->visible = 0;
        return;
    }
    gtk_widget_show(GTK_WIDGET(app->window));
    app->visible = 1;
}

void apply_fan_curve(GtkWidget *object, gpointer data) {
    application *app = data;
    fan_curve *curve = app->curve;
    // all values that may change
    int old_scan;
    old_scan = curve->scan;
    if (!set_curve_values(curve)) {
        fprintf(stderr, "Curve input is incorrect");
        return;
    }
    // first check if we are already running a curve:
    if (app->running && app->manual == 2) {
        // we are already running the curve. If the interval hasn't changed, then the next callback will apply the new curve values
        // so we don't really need to check any of the other values anyway
        if (old_scan != curve->scan) {
            // new interval, set the timeout
            g_source_remove(app->timeout);
            // we do want update_temps to handle this stuff better, this callback will probably be changed
            app->timeout = g_timeout_add_seconds(app->scan_interval, update_temps, data);
            return;
        }
        // we can just ignore this, the change will be picked up next time the callback is invoked
        return;
    }
    // now, are we running?
    if (app->running) {
        // yes, remove timeout
        g_source_remove(app->timeout);
    } else {
        app->fan_speed = FAN_LVL_AUTO; // assume auto -> this is most likely the value on startup
        gtk_widget_set_sensitive(GTK_WIDGET(app->off_btn), TRUE);
    }
    // either way, now we are running, in mode 2
    app->running = 1;
    app->manual = 2;
    if (update_temps(data)) {
        // set timeout
        app->timeout = g_timeout_add_seconds(app->curve->scan, update_temps, data);
    }
}

// apply new auto config
void apply_auto_speed(GtkWidget *object, gpointer data) {
    int old_safe, old_crit, old_fan_speed, old_interval;
    char config_str[80];
    application *app = data;
    if (data == NULL) {
        fprintf(stderr, "NO DATA POINTER PASSED");
        return;
    }
    old_safe = app->temp_safe;
    old_crit = app->temp_crit;
    old_fan_speed = app->fan_speed;
    old_interval = app->scan_interval;
    // get the values we want to apply:
    // let's see if the old profile was actually changed
    if (!set_auto_values(app)) {
        fprintf(stderr, "WRONG INPUT");
        return;
    }
    // should we actually do anything? if we are running, not in manual, and no values were changed, we are done
    if (app->running && !app->manual && old_safe == app->temp_safe && old_crit == app->temp_crit && old_fan_speed == app->fan_speed && old_interval == app->scan_interval) {
        return;
    }
    // now we know something has changed, and so we MUST do something
    if (app->running) {
        // we were running, so clear the timeout
        g_source_remove(app->timeout);
    } else {
        gtk_widget_set_sensitive(GTK_WIDGET(app->off_btn), TRUE);
    }
    sprintf(config_str, AUTO_LBL_FMT, app->scan_interval, app->temp_crit, app->temp_safe, fan_speeds[app->fan_speed]);
    gtk_label_set_text(app->auto_lbl, config_str);
    // if we were able to apply the new profile successfully...
    app->running = 1;
    app->manual = 0;
    if (update_temps(data)) {
        app->timeout = g_timeout_add_seconds(app->scan_interval, update_temps, data);
    }
}

// apply manual fan speed
void apply_manual_speed(GtkWidget *object, gpointer data) {
    application *app = data;
    int old_fan_speed = app->fan_speed;
    int old_interval = app->scan_interval; // check if we need to update this
    // get current values from input
    set_manual_values(app);
    // first, make sure we aren't already running in manual mode, and nothing has changed
    if (app->running && app->manual && app->fan_speed == old_fan_speed && app->scan_interval == old_interval) {
        return;
    }
    if (app->running) {
        // remove timeout...
        g_source_remove(app->timeout);
    } else {
        // we were not yet running fan control, we are now, so enable the stop button
        gtk_widget_set_sensitive(GTK_WIDGET(app->off_btn), TRUE);
    }
    // first up, let's change the fan speed
    change_fan_speed(app->fan_speed, app);
    // mark as running, manually
    app->running = 1;
    app->manual = 1;
    if (update_temps(data)) {
        app->timeout = g_timeout_add_seconds(app->scan_interval, update_temps, data);
    }
}

void change_fan_speed(int new_speed, application *app) {
    char speed_str[15], tmp_str[80];
    int actual_level = new_speed;

    // Map array index to actual fan level
    // Index 0 = auto, Index 1 = level 0, Index 2-8 = levels 1-7, Index 9 = full-speed
    if (new_speed >= 1 && new_speed < FAN_LVL_FULL) {
        actual_level = new_speed - 1;
    }

    switch(new_speed) {
        case 0:
            strncpy(speed_str, "auto", 5);
            break;
        case FAN_LVL_FULL:
            strncpy(speed_str, "full-speed", 11);
            break;
        default:
            sprintf(speed_str, "%d", actual_level);
    }
    sprintf(tmp_str, "echo level %s > /proc/acpi/ibm/fan", speed_str);
    system(tmp_str);
    if (app->print_logs)
        printf("Fan speed set to %s - config specified: %s\n", fan_speeds[new_speed], fan_speeds[app->fan_speed]); // this is a bit of an odd message when in curve mode
}

static void
curve_value_changed(GtkScale *scale, gpointer obj) {
    fan_curve *curve = obj;
    gdouble value = gtk_range_get_value(GTK_RANGE(scale));
    curve->throttle_factor = value/100;
    // printf("New value is %f\n", value);
}

int curve_fan_speed_for_temp(fan_curve *curve, int temp) {
    int speed, step, max, bracket, f_step;
    // If the temperature drops below a certain threshold, we don't want to instantly throttle down
    // we want to make sure we drop below said temp by a given factor
    // safe temperature can do whatever, we just ramp down, no factor applied
    f_step = (int) ((double) curve->delta_temp * curve->throttle_factor);
    speed = curve->safe_speed;
    step = curve->step;
    bracket = curve->safe_temp;
    max = curve->crit_temp;
    // for critical, or safe temps, we don't check the factor.
    if (temp >= max) {
        // critical speed reached, return here
        return curve->crit_speed;
    } else if (temp <= bracket) {
        // bracket is safe temp here, so we know whether or not we can throttle down immediately
        return speed;
    }
    // now the temp measured should be increased by the factor
    temp += f_step;
    while(bracket < temp) {
        bracket += curve->delta_temp;
        speed += step;
        // step is above full
        if (speed > FAN_LVL_FULL) {
            fprintf(stderr, "looks like it was possible to hit full fan speed before hitting critical temp\n");
            return FAN_LVL_FULL;
        }
    }
    return speed;
}

// timeout callback, keeps being called while  we are actually running
int update_temps(gpointer data) {
    application *app = data; // this gives us access to the components, mode, and so on
    char tmp_string[250] = {'\0'};
    char time_str[10] = {'\0'};
    char message[100] = {'\0'};
    FILE *sys_in;
    // get current CPU temp
    int temp = get_cpu_temp(app);
    if (temp == -1) {
        return FALSE;
    }
    // get current timestamp
    sys_in = popen("date '+%H:%M:%S'","r");
    fgets(time_str, 9, sys_in);
    pclose(sys_in);

    sprintf(message, "CPU Temp: %d C, Checked at %s", temp, time_str);
    if (app->manual == 1) {
        // full message for status bar:
        sprintf(tmp_string, "Manual control is active! - %s", message);
    } else if (app->manual == 2) {
        sprintf(tmp_string, "Curve control is active! - %s", message);
    } else {
        sprintf(tmp_string, "Automatic control - %s", message);
    }
    // push temp to status bar
    gtk_statusbar_remove(app->status_bar, 0, app->status_id);
    app->status_id = gtk_statusbar_push(app->status_bar, 0, tmp_string);
    if (app->manual == 1) {
        sprintf(tmp_string,"Fan level - %s\n%s", fan_speeds[app->fan_speed], message);
    } else if (app->manual == 2) {
        // this is where we handle the fan curve stuff, starting with the simple things:
        if ((temp + app->curve->delta_temp) <= app->curve->safe_temp) {
            // on or below safe + 1 delta
            if (app->fan_speed != app->curve->safe_speed) {
                change_fan_speed(app->curve->safe_speed, app);
                app->fan_speed = app->curve->safe_speed;
            }
            sprintf(tmp_string,"Fan level - %s\n%s", fan_speeds[app->fan_speed], message);
        } else if (temp >= app->curve->crit_temp) {
            if (app->fan_speed != app->curve->crit_speed) {
                change_fan_speed(app->curve->crit_speed, app);
                app->fan_speed = app->curve->crit_speed;
            }
            sprintf(tmp_string,"Fan level - %s\n%s", fan_speeds[app->fan_speed], message);
        } else {
            app->curve->fan_speed = curve_fan_speed_for_temp(app->curve, temp);
            // and here's where we need to calculate the steps, and corresponding fan speed
            if (app->curve->fan_speed == app->fan_speed) {
                // no changes in fan speed
                sprintf(tmp_string,"Fan level - %s\n%s", fan_speeds[app->fan_speed], message);
            } else {
                if (app->curve->fan_speed < app->fan_speed) {
                    sprintf(tmp_string,"Fan level down- %s\n%s", fan_speeds[app->curve->fan_speed], message);
                } else {
                    sprintf(tmp_string,"Fan level up - %s\n%s", fan_speeds[app->curve->fan_speed], message);
                }
                change_fan_speed(app->curve->fan_speed, app);
                app->fan_speed = app->curve->fan_speed;
            }
        }
    } else {
        if (temp >= app->temp_crit && !app->was_crit) {
            // we are just now running too hot, ramp up fans
            change_fan_speed(app->fan_speed, app);
            app->was_crit = 1;
            sprintf(tmp_string, "Temperature is critical, Fan level set to %s\n%s", fan_speeds[app->fan_speed], message);
        } else if (app->was_crit) {
            // cooled down to below crit, but we WERE running at crit speed -> move back to auto
            change_fan_speed(FAN_LVL_AUTO, app);
            app->was_crit = 0;
            sprintf(tmp_string, "Temperature is safe, Fan level set to %s\n%s", fan_speeds[FAN_LVL_AUTO], message);
        } else if (!app->was_crit) {
            // we're below critical
            sprintf(tmp_string,"SAFE - Fan level:  %s\n%s", fan_speeds[FAN_LVL_AUTO], message);
        } else {
            // still above critical
            sprintf(tmp_string,"CRITICAL - Fan level: %s\n%s", fan_speeds[app->fan_speed], message);
        }
    }
    // set label accordingly
    gtk_label_set_text(app->current_lbl, tmp_string);
    return TRUE;
}

static GtkStatusIcon *create_tray_icon() {
    GtkStatusIcon *tray_icon = gtk_status_icon_new();

    // g_signal_connect(G_OBJECT(tray_icon), "popup-menu",G_CALLBACK(tray_icon_on_menu), NULL);
    gtk_status_icon_set_from_file(tray_icon, "data/icon.png");
    // gtk_status_icon_set_tooltip(tray_icon, "ThinkPad Fan Control .:: level - auto ::.");
    gtk_status_icon_set_visible(tray_icon, TRUE);

    return tray_icon;
}

void notebook_switch(GtkNotebook *nb, GtkWidget *page, guint page_num, gpointer data) {
    application *app = data;
    if (app->running) {
        // we're running - so we don't have to update the label
        return;
    }
    char current_txt[80];
    const char *lbl_fmt = "%s\nCPU temp: %d"; // max should be below 80
    int temp = get_cpu_temp(app);
    if (temp == -1) {
        // error, the label has been set with the error message, we're done
        return;
    }
    switch (page_num) {
        case 0:
            sprintf(current_txt, lbl_fmt, "Hit apply to run automatic control with specified settings", temp); // ~60 chars + 2-3 digits for temp
            break;
        case 1:
            sprintf(current_txt, lbl_fmt, "Hit apply to force the selected fan speed", temp);
            break;
        case 2:
            sprintf(current_txt, lbl_fmt, "Apply the configured fan curve", temp);
            break;
        default:
            sprintf(current_txt, lbl_fmt, "", temp);
    }
    gtk_label_set_text(app->current_lbl, current_txt);
}

void stop_fan_monitor(GtkWidget *object, gpointer data) {
    application *app = data;
    if (app->running) {
        g_source_remove(app->timeout);
    }
    if (app->fan_speed != FAN_LVL_AUTO) {
        app->fan_speed = FAN_LVL_AUTO;
        change_fan_speed(FAN_LVL_AUTO, app);
    }
    // this may not always be needed, but still, doesn't hurt
    app->running = 0;
    app->manual = 0;
    // set the labels to whatever values we need them to be for the text to be relevant
    notebook_switch(app->main_nb, NULL, gtk_notebook_get_current_page(app->main_nb), app); // the page is irrelevant, we can safely pass in NULL
    // disable button
    gtk_widget_set_sensitive(object, FALSE);
}

void dialog_yes(GtkButton *close_y, gpointer data) {
    application *app = data;
    gtk_widget_hide(GTK_WIDGET(app->close));
    change_fan_speed(FAN_LVL_AUTO, app);
    exit_app(app);
}

void dialog_no(GtkButton *close_n, gpointer data) {
    application *app = data;
    gtk_widget_hide(GTK_WIDGET(app->close));
    exit_app(app);
}

void dialog_close(GtkButton *close_c, gpointer data) {
    application *app = data;
    gtk_widget_hide(GTK_WIDGET(app->close));
    // no call to exit_app, timeout (if any) remains active
    // just hide the close dialog widget, and carry on
}

void print_help(char const *bin) {
    printf("%s Usage:\n\
            -v : Verbose, print logs to stdout (default false)\n\
            -h : Help - display this message\n", bin);
}

int main(int argc, char** argv) {
    bool print_logs = false;
    int c;
    GError *err = NULL;
    GtkWidget *window;
    GtkDialog *close;
    GtkNotebook *main_nb;
    GtkButton *exit_btn, *minimize_btn, *man_speed_btn, *auto_speed_btn, *close_y, *close_n, *close_c, *curve_apply_btn, *off_btn;
    GtkBuilder *builder;
    GtkStatusIcon *tray_icon;
    GtkStatusbar *status_bar;
    GtkLabel *current_settings_lbl, *auto_settings_lbl, *close_lbl;
    GtkComboBox *auto_speed, *manual_speed;
    GtkSpinButton *auto_interval_sbtn, *crit_sbtn, *safe_sbtn, *man_interval_sbtn;
    guint status_id;
    char const* bin = argv[0];

    while ((c = getopt(argc, argv, "vh")) != -1) {
        switch (c) {
            case 'v':
                print_logs = true;
                break;
            case 'h':
                print_help(bin);
                return 0;
            case '?':
                print_help(bin);
                if (isprint(optopt)) {
                    fprintf(stderr, "Unknown option `-%c'.\n", optopt);
                } else {
                    fprintf(stderr, "Unknown option character `\\x%x'.\n", optopt);
                }
                return 1;
            default:
                abort();
        }
    }
    gtk_init(&argc, &argv);

    tray_icon = create_tray_icon();

    builder = gtk_builder_new();
    // make sure this all works
    if (gtk_builder_add_from_file(builder, GTK_GUI_FILE, &err) == 0) {
        fprintf(stderr, "Error handling build from file. Error: %s\n", err->message);
        g_object_unref(G_OBJECT(builder));
        return 1;
    }
    // we don't check for errors here, because these ID's are just known to exist
    // Ideally we check each time for window or exit_btn and the like to not be NULL, but come on...
    window = GTK_WIDGET(gtk_builder_get_object(builder, "window"));
    close = GTK_DIALOG(gtk_builder_get_object(builder, "close_dialog"));
    main_nb = GTK_NOTEBOOK(gtk_builder_get_object(builder, "main_notebook"));
    current_settings_lbl = GTK_LABEL(gtk_builder_get_object (builder, "current_settings_lbl"));
    auto_settings_lbl = GTK_LABEL(gtk_builder_get_object (builder, "auto_ctrl_lbl"));
    status_bar = GTK_STATUSBAR(gtk_builder_get_object(builder, "status_bar"));  
    close_lbl = GTK_LABEL(gtk_builder_get_object(builder, "close_current_speed_lbl"));

    auto_speed = GTK_COMBO_BOX(gtk_builder_get_object(builder, "fan_speed_crit_cmb"));  
    manual_speed = GTK_COMBO_BOX(gtk_builder_get_object(builder, "man_fan_speed_cmb"));  

    auto_interval_sbtn = GTK_SPIN_BUTTON(gtk_builder_get_object(builder, "auto_scan_int_sbtn"));
    crit_sbtn = GTK_SPIN_BUTTON(gtk_builder_get_object(builder, "crit_tmp_sbtn"));
    safe_sbtn = GTK_SPIN_BUTTON(gtk_builder_get_object(builder, "safe_tmp_sbtn"));
    man_interval_sbtn = GTK_SPIN_BUTTON(gtk_builder_get_object(builder, "man_scan_int_sbtn"));
    off_btn = GTK_BUTTON(gtk_builder_get_object(builder, "off_btn"));

    status_id = gtk_statusbar_push(status_bar, 0, "Welcome!");
    fan_curve curve = {
        .config = GTK_LABEL(gtk_builder_get_object(builder, "grad_config_lbl")),
        .safe_cmb = GTK_COMBO_BOX(gtk_builder_get_object(builder, "grad_safe_speed_cmb")),
        .crit_cmb = GTK_COMBO_BOX(gtk_builder_get_object(builder, "grad_crit_speed_cmb")),
        .inc_cmb = GTK_COMBO_BOX(gtk_builder_get_object(builder, "grad_fan_inc_cmb")),
        .safe = GTK_SPIN_BUTTON(gtk_builder_get_object(builder, "grad_safe_temp_sbtn")),
        .crit = GTK_SPIN_BUTTON(gtk_builder_get_object(builder, "grad_crit_temp_sbtn")),
        .scan_int = GTK_SPIN_BUTTON(gtk_builder_get_object(builder, "grad_int_sbtn")),
        .delta = GTK_SPIN_BUTTON(gtk_builder_get_object(builder, "grad_temp_inc_sbtn")),
        .throttle_scl = GTK_SCALE(gtk_builder_get_object(builder, "grad_throttle_scale")),
    };
    // everything we might need in the callbacks, passed as gpointer
    application app = {
        .window = window,
        .close = close,
        .status_bar = status_bar,
        .main_nb = main_nb,
        .tray_icon = tray_icon,
        .current_lbl = current_settings_lbl,
        .auto_lbl = auto_settings_lbl,
        .auto_cmb = auto_speed,
        .close_lbl = close_lbl,
        .man_cmb = manual_speed,
        .auto_int = auto_interval_sbtn,
        .man_int = man_interval_sbtn,
        .crit = crit_sbtn,
        .safe = safe_sbtn,
        .status_id = status_id,
        .running = 0,
        .visible = 0,
        .was_crit = 0,
        .manual = 0,
        .timeout = 0,
        .curve = &curve,
        .off_btn = off_btn,
        .print_logs = print_logs,
    };

    // Get buttons
    exit_btn = GTK_BUTTON(gtk_builder_get_object(builder, "exit_btn"));
    minimize_btn = GTK_BUTTON (gtk_builder_get_object(builder, "minimize_btn"));
    man_speed_btn = GTK_BUTTON(gtk_builder_get_object(builder, "man_apply_btn"));
    auto_speed_btn = GTK_BUTTON(gtk_builder_get_object(builder, "auto_ctrl_apply_btn"));
    curve_apply_btn = GTK_BUTTON(gtk_builder_get_object(builder, "grad_apply_btn"));
    close_y = GTK_BUTTON(gtk_builder_get_object(builder, "close_auto_yes_btn"));
    close_n = GTK_BUTTON(gtk_builder_get_object(builder, "close_auto_no_btn"));
    close_c = GTK_BUTTON(gtk_builder_get_object(builder, "close_auto_cancel_btn"));

    // format throttle scale, pass in the curve struct so we can update the factor field
    g_signal_connect(G_OBJECT(curve.throttle_scl), "value-changed", G_CALLBACK(curve_value_changed), &curve);
    // connect signals
    // minimize, hide window, tray icon stuff
    g_signal_connect(G_OBJECT(tray_icon), "activate", G_CALLBACK(hide_window), &app);
    g_signal_connect(G_OBJECT(minimize_btn), "clicked", G_CALLBACK(hide_window), &app);
    // changeing notebook pages
    g_signal_connect(G_OBJECT(main_nb), "switch-page", G_CALLBACK(notebook_switch), &app);
    // apply changes buttons
    g_signal_connect(G_OBJECT(man_speed_btn), "clicked", G_CALLBACK(apply_manual_speed), &app);
    g_signal_connect(G_OBJECT(auto_speed_btn), "clicked", G_CALLBACK(apply_auto_speed), &app);
    g_signal_connect(G_OBJECT(curve_apply_btn), "clicked", G_CALLBACK(apply_fan_curve), &app);
    // button is only enabled if the app state is set to running
    g_signal_connect(G_OBJECT(off_btn), "clicked", G_CALLBACK(stop_fan_monitor), &app);
    // Exit button click
    g_signal_connect(G_OBJECT(exit_btn), "clicked", G_CALLBACK(window_destroy), &app);
    // close dialog signals:
    g_signal_connect(G_OBJECT(close_y), "clicked", G_CALLBACK(dialog_yes), &app);
    g_signal_connect(G_OBJECT(close_n), "clicked", G_CALLBACK(dialog_no), &app);
    g_signal_connect(G_OBJECT(close_c), "clicked", G_CALLBACK(dialog_close), &app);
    // handle closing of the window
    g_signal_connect(G_OBJECT(window), "destroy", G_CALLBACK(window_destroy), &app);

    // connect signals && unref builder
    gtk_builder_connect_signals(builder, &app); // AFAIK, we don't really need this
    g_object_unref(G_OBJECT(builder));
    
    // set curve values
    set_curve_values(&curve);
    if (!set_auto_values(&app)) {
        fprintf(stderr, "DEFAULT PROFILE VALUES ARE WRONG");
    }
    // populate label with current fan control
    notebook_switch(main_nb, NULL, 0, &app); // the page is irrelevant, we can safely pass in NULL
    // show window
    gtk_widget_show(window);

    // hand over to gtk
    gtk_main();
    return 0;
}
