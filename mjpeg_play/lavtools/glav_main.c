#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <gtk/gtk.h>
#include "glav.h"
#include <gdk/gdkkeysyms.h>
#include <unistd.h>

#define PLAY_PROG "lavplay"
#define LAVPLAY_VSTR "lavplay" LAVPLAY_VERSION /* Expected version info */

int verbose = 1;
static GTK_xlav *gtk_xlav;

static int inp_pipe;
static int out_pipe;

static int pid;

static int norm, cur_pos, total_frames, cur_speed=1, old_speed=999999;
static int slider_pause = 0;

static int ff_stat=0, fr_stat=0;
static int ff_speed[4] = { 1, 3, 10, 30 };

static int selection_start = -1;
static int selection_end = -1;

#define MAXINP 4096
#define SAVE_ALL 1
#define SAVE_SEL 2

static int savetype = 0;
static char inpbuff[MAXINP];
static int inplen = 0;

static int hscale_down = 0;
static int frame_skip_button_up = 1;
static char frame_skip_char;

static char timecode[64];
char *selected_filename;
GtkWidget *file_selector;

static void skip_num_frames(int num) {
   char out[32];
   sprintf(out,"s%d\n",cur_pos + num);
   write(out_pipe,out,strlen(out));
}

static void calc_timecode(int pos, int do_frames)
{
   int h, m, s, f;

   /* Calculate hours:min:sec:frames */

   if(norm=='n')
   {
      f = cur_pos%30;
      s = cur_pos/30;
   }
   else
   {
      f = cur_pos%25;
      s = cur_pos/25;
   }
   m = s/60;
   s = s%60;
   h = m/60;
   m = m%60;
   if (!do_frames) f=0;
   sprintf(timecode,"%2d:%2.2d:%2.2d:%2.2d",h,m,s,f);
}

void store_filename(GtkFileSelection *selector, gpointer user_data) {
   char str[256];
   const char *name;
   selected_filename = gtk_file_selection_get_filename (GTK_FILE_SELECTION(file_selector));
   name = selected_filename;
   if(name==0) return;
   switch(savetype) {
      case SAVE_ALL:
         sprintf(str,"wa %s\n",name); break;
      case SAVE_SEL:
         sprintf(str,"ws %d %d %s\n",selection_start,selection_end,name); break;
   }
   write(out_pipe,str,strlen(str));
   printf("Wrote to %s\n",name);
}

void create_file_selection() {
   /* Create the selector */
   char label[32];
   switch (savetype) {
      case SAVE_ALL: sprintf(label,"save all to file..."); break;
      case SAVE_SEL: sprintf(label,"save selected to file..."); break;
      default: printf("Error in create_file_selection\n"); return; break;
   }
   file_selector = gtk_file_selection_new(label);
   gtk_signal_connect (GTK_OBJECT(GTK_FILE_SELECTION(file_selector)->ok_button),
               "clicked", GTK_SIGNAL_FUNC (store_filename), NULL);
   /* Ensure that the dialog box is destroyed when the user clicks a button. */
   gtk_signal_connect_object (GTK_OBJECT (GTK_FILE_SELECTION(file_selector)->ok_button),
                    "clicked", GTK_SIGNAL_FUNC (gtk_widget_destroy),
                    (gpointer) file_selector);
   gtk_signal_connect_object (GTK_OBJECT (GTK_FILE_SELECTION(file_selector)->cancel_button),
                    "clicked", GTK_SIGNAL_FUNC (gtk_widget_destroy),
                    (gpointer) file_selector);
   
   /* Display that dialog */
   gtk_widget_show (file_selector);
}

gint key_press_cb(GtkWidget * widget, GdkEventKey* event, gpointer data ) {

   int need_pause=FALSE;
   hscale_down=1;
   switch (event->keyval) {
      case GDK_Home: // go to beginning of file
      case GDK_0:
         gtk_adjustment_set_value(GTK_ADJUSTMENT(gtk_xlav->timeslider),(gfloat)0);
         break;
      case GDK_End: // go to end of file
      case GDK_dollar:
         need_pause=TRUE;
         write(out_pipe,"s10000000\n",10); break;  // go to end
         break;
      case GDK_parenleft: // mark start of selection
         gtk_signal_emit_by_name(GTK_OBJECT(gtk_xlav->BSSelStart),"clicked",(gpointer)1);
         break;
      case GDK_parenright: // mark end of selection
         gtk_signal_emit_by_name(GTK_OBJECT(gtk_xlav->BSSelEnd),"clicked",(gpointer)1);
         break;
      case GDK_l: // 1 frame right
      case GDK_Right:
         need_pause=TRUE;
         skip_num_frames(1);
         break;
      case GDK_h: // 1 frame left
      case GDK_Left:
         need_pause=TRUE;
         skip_num_frames(-1);
         break;
      case GDK_w: // (word) 15 frames right
         need_pause=TRUE;
         skip_num_frames(15);
         break;
      case GDK_b: // (back) 15 frames left
         need_pause=TRUE;
         skip_num_frames(-15);
         break;
      case GDK_W: // 30 frames right
         need_pause=TRUE;
         skip_num_frames(30);
         break;
      case GDK_B: // 30 frames left
         need_pause=TRUE;
         skip_num_frames(-30);
         break;
      case GDK_x: // cut selection
      case GDK_Delete:
         gtk_signal_emit_by_name(GTK_OBJECT(gtk_xlav->BECut),"clicked",(gpointer)1);
         break;
      case GDK_y: // copy (yank) selection
         gtk_signal_emit_by_name(GTK_OBJECT(gtk_xlav->BECopy),"clicked",(gpointer)1);
         break;
      case GDK_p: // paste selection
         gtk_signal_emit_by_name(GTK_OBJECT(gtk_xlav->BEPaste),"clicked",(gpointer)1);
         break;
      case GDK_f: //  play (forward)
         gtk_signal_emit_by_name(GTK_OBJECT(gtk_xlav->play),"clicked",(gpointer)4);
         break;
      case GDK_F: // fast forward
         gtk_signal_emit_by_name(GTK_OBJECT(gtk_xlav->ff),"clicked",(gpointer)5);
         break;
      case GDK_r: // play reverse
         gtk_signal_emit_by_name(GTK_OBJECT(gtk_xlav->rew),"clicked",(gpointer)2);
         break;
      case GDK_R: // fast reverse
         gtk_signal_emit_by_name(GTK_OBJECT(gtk_xlav->fr),"clicked",(gpointer)1);
         break;
      case GDK_s: // stop
      case GDK_S:
         gtk_signal_emit_by_name(GTK_OBJECT(gtk_xlav->stop),"clicked",(gpointer)3);
         break;
      case GDK_1: // go 5 seconds forward
         skip_num_frames(150);
         break;
      case GDK_2: // go 10 seconds forward
         skip_num_frames(300);
         break;
      case GDK_3: // go 15 seconds forward
         skip_num_frames(450);
         break;
      case GDK_exclam: // go 5 seconds back
         skip_num_frames(-150);
         break;
      case GDK_at: // go 10 seconds back
         skip_num_frames(-300);
         break;
      case GDK_numbersign: // go 15 seconds back
         skip_num_frames(-450);
         break;
      case GDK_Shift_L: // just shift keys, eat them
      case GDK_Shift_R:
         break;
      default:
         break;
   }
   if (cur_speed!=0) {
      if (need_pause) {
         write(out_pipe,"p0\n",3); // pause on all keys
      }
   }
   hscale_down=0;
}

void quick_message(char *message) {

   GtkWidget *dialog, *label, *okay_button;
   
   /* Create the widgets */
   
   dialog = gtk_dialog_new();
   label = gtk_label_new (message);
   okay_button = gtk_button_new_with_label("Okay");
   
   /* Ensure that the dialog box is destroyed when the user clicks ok. */
   
   gtk_signal_connect_object (GTK_OBJECT (okay_button), "clicked",
                              GTK_SIGNAL_FUNC (gtk_widget_destroy), GTK_OBJECT(dialog));
   gtk_container_add (GTK_CONTAINER (GTK_DIALOG(dialog)->action_area),
                      okay_button);

   /* Add the label, and show everything we've added to the dialog. */

   gtk_container_add (GTK_CONTAINER (GTK_DIALOG(dialog)->vbox),
                      label);
   gtk_widget_show_all (dialog);
}



void dispatch_input(void)
{
	char normc;
   /* A line starting with '-' should be ignored */

   if(inpbuff[0]=='-') return;

   /* A line starting with '@' contains psoition information */

   if(inpbuff[0]=='@')
   {
      double cp, tf;
      sscanf(inpbuff+1,"%c%d/%d/%d",&normc,&cur_pos,&total_frames,&cur_speed);
	  norm = normc;
      calc_timecode(cur_pos,cur_speed==0);
      gtk_label_set_text(GTK_LABEL(gtk_xlav->Timer),timecode);
      // fl_set_object_label(gtk_xlav->Timer,timecode);
      if(total_frames<1) total_frames=1;
      if(slider_pause) {
         slider_pause--;

      } else {
         if (!hscale_down) {
         gtk_adjustment_set_value(GTK_ADJUSTMENT(gtk_xlav->timeslider),(100.0)*(double) ((double)cur_pos)/((double)total_frames));
         }
      }
      if(cur_speed != old_speed) {
         char label[32];

         if(cur_speed == 1) {
            gtk_label_set_text(GTK_LABEL(gtk_xlav->StatDisp),"Play >");
         } else if(cur_speed == 0) {
            gtk_label_set_text(GTK_LABEL(gtk_xlav->StatDisp),"Pause");
         } else if(cur_speed == -1) {
            gtk_label_set_text(GTK_LABEL(gtk_xlav->StatDisp),"Play <");
         } else if(cur_speed < -1) {
            sprintf(label,"<<%2dx",-cur_speed);
            gtk_label_set_text(GTK_LABEL(gtk_xlav->StatDisp),label);
         } else if(cur_speed > 1) {
            sprintf(label,">>%2dx",cur_speed);
            gtk_label_set_text(GTK_LABEL(gtk_xlav->StatDisp),label);
         }
         old_speed = cur_speed;
      }
      return;
   }
   // Note: no need to pass through ordinary lavplay logging output as it comes through
   // stderr not stdout...

}

void get_input(gpointer data, gint fd, GdkInputCondition condition) 
{
   char input[4096];
   int i, n;

   n = read(fd,input,4096);
   if(n==0) exit(0);

   for(i=0;i<n;i++)
   {
      if(inplen<MAXINP-1) inpbuff[inplen++] = input[i];
      if(input[i]=='\n')
      {
         inpbuff[inplen] = 0;
         dispatch_input();
         inplen = 0;
      }
   }
}


void        timehscale_button_pressed_cb(GtkAdjustment *adjustment, gpointer data) {
   hscale_down=1;
}

void        timehscale_button_released_cb(GtkAdjustment *adjustment, gpointer data) {
   hscale_down=0;
}

void        timeslider_cb(GtkAdjustment *adjustment, gpointer data)
{
   gfloat val;
   char out[256];

   if (hscale_down) {
      float new;
      val = ((GTK_ADJUSTMENT(gtk_xlav->timeslider)->value));
      new = (val / 100.00 );
      sprintf(out,"s%d\n",(int)((val*total_frames)/100));
      write(out_pipe,out,strlen(out));
   }
      slider_pause = 8;
}

void button_cb(GtkWidget *ob, long data)
{
   switch(data)
   {
      case 1: write(out_pipe,"s0\n",3); break; // go to beginning
      case 2: write(out_pipe,"s10000000\n",10); break;  // go to end
      /* // moved this to  frame_skip_pressed
      case 3: write(out_pipe,"-\n",2); break;
      case 4: write(out_pipe,"+\n",2); break;
      */
   }
}

guint skip_frame(char *mychar) {
   if (frame_skip_button_up == 0 ) {
      char out[10];
      sprintf(out,"%c\n",frame_skip_char);
      write(out_pipe,out,2); 
   }
   return (! frame_skip_button_up);
}

void frame_skip_pressed(GtkWidget *ob, long data) {
      frame_skip_button_up=0;
      switch(data) {
         case 3: 
            frame_skip_char='-';  // frame reverse
            gtk_timeout_add(10,(GtkFunction)skip_frame,(gpointer)0);
            break;
         case 4:
            frame_skip_char='+'; // frame advance
            gtk_timeout_add(10,(GtkFunction)skip_frame,(gpointer)0);
            break;
         default: break;
      }
}

void frame_skip_released(GtkWidget *ob, long data){
   frame_skip_button_up=1;
}


void rb_cb(GtkWidget *ob, long data)
{
   char out[32];

   if (data!=1) fr_stat = 0;
   if (data!=5) ff_stat = 0;

   switch(data)
   {
      case 1:
         fr_stat++;
         if(fr_stat>3) fr_stat=1;
         sprintf(out,"p-%d\n",ff_speed[fr_stat]);
         write(out_pipe,out,strlen(out));
         break;
      case 2: write(out_pipe,"p-1\n",4); break;
      case 3: write(out_pipe,"p0\n",3); break;
      case 4: write(out_pipe,"p1\n",3); break;
      case 5:
         ff_stat++;
         if(ff_stat>3) ff_stat=1;
         sprintf(out,"p%d\n",ff_speed[ff_stat]);
         write(out_pipe,out,strlen(out));
         break;
      case 0:
         // this is here for a callback that does nothing
         break;
      default:
         break;
   }
}

#if 0 /* No Audio mute at the moment */
void Audio_cb(GtkWidget *ob, long data)
{
   if(fl_get_button(gtk_xlav->Audio))
      write(out_pipe,"a1\n",3);
   else
      write(out_pipe,"a0\n",3);
}
#endif

void do_real_exit(int ID, void *data)
{
   int status;

   /* Kill all our children and exit */

   printf("real exit here\n");
   kill(0,9);
   waitpid(pid,&status,0);
   exit(0);
}

void Exit_cb(GtkWidget *ob, long data)
{
   /* Try to exit gracefully, wait 1 second, do real exit */

   write(out_pipe,"q\n\n\n",4);
   gtk_timeout_add(1000,(GtkFunction)do_real_exit,0);
}

void signal_cb(int signum, void *data)
{
   Exit_cb(0,0);
}

static int check_selection()
{
   if(selection_start>=0 && selection_end>=selection_start) return 0;
   quick_message("Selection invalid!!!");
   return -1;
}


void selection_cb(GtkWidget *ob, long data)
{
   char str[256];
   const char *name;

   switch(data)
   {
      case 1:
         selection_start = cur_pos;
         calc_timecode(cur_pos,1);
         gtk_label_set_text(GTK_LABEL(gtk_xlav->FSelStart),timecode);
         break;
      case 2:
         selection_end   = cur_pos;
         calc_timecode(cur_pos,1);
         gtk_label_set_text(GTK_LABEL(gtk_xlav->FSelEnd),timecode);
         break;
      case 3: // Clear
         selection_start = -1;
         selection_end   = -1;
         gtk_label_set_text(GTK_LABEL(gtk_xlav->FSelStart),"-:--:--:--");
         gtk_label_set_text(GTK_LABEL(gtk_xlav->FSelEnd),"-:--:--:--");
         break;
      case 4: // Cut
      case 5: // Copy
         if(check_selection()) return;
         sprintf(str,"e%c %d %d\n",(data==4)?'u':'o',selection_start,selection_end);
         write(out_pipe,str,strlen(str));
         if(data==4)
         {
            selection_start = -1;
            selection_end   = -1;
            gtk_label_set_text(GTK_LABEL(gtk_xlav->FSelStart),"-:--:--:--");
            gtk_label_set_text(GTK_LABEL(gtk_xlav->FSelEnd),"-:--:--:--");
         }
         break;
      case 6: // Paste
         if(check_selection()) return;
         selection_start = -1;
         selection_end   = -1;
         gtk_label_set_text(GTK_LABEL(gtk_xlav->FSelStart),"-:--:--:--");
         gtk_label_set_text(GTK_LABEL(gtk_xlav->FSelEnd),"-:--:--:--");
         write(out_pipe,"ep\n",3);
         break;
      case 7: // Save All
         savetype=SAVE_ALL;
         create_file_selection();
         name = selected_filename;
         if(name==0) return;
         sprintf(str,"wa %s\n",name);
         write(out_pipe,str,strlen(str));
         break;
      case 8: // Save 
         if(check_selection()) return;
         savetype=SAVE_SEL;
         create_file_selection();
         name = selected_filename;
         if(name==0) return;
         sprintf(str,"ws %d %d %s\n",selection_start,selection_end,name);
         write(out_pipe,str,strlen(str));
         break;
      case 11:
         if(selection_start >= 0)
         {
            sprintf(str,"s%d\n",selection_start);
            write(out_pipe,str,strlen(str));
         }
         else
            printf("Selection Start is not set!\n");
         break;
      case 12:
         if(selection_end >= 0)
         {
            sprintf(str,"s%d\n",selection_end);
            write(out_pipe,str,strlen(str));
         }
         else
            printf("Selection End is not set!\n");
         break;
      default:
         printf("selection %ld\n",data);
   }
}

void create_child(char **args)
{
   int ipipe[2], opipe[2];
   int n, vlen;
   char version[32];

   if(pipe(ipipe)!=0 || pipe(opipe)!=0) { perror("Starting "PLAY_PROG); exit(1); }

   pid = fork();

   if(pid<0) { perror("Starting "PLAY_PROG); exit(1); }

   if (pid)
   {
      /* We are the parent */

      inp_pipe = ipipe[0];
      close(ipipe[1]);
      out_pipe = opipe[1];
      close(opipe[0]);
   }
   else
   {
      /* We are the child */

      close(ipipe[0]);
      close(opipe[1]);

      close(0);
      n = dup(opipe[0]);
      if(n!=0) exit(1);

      close(opipe[0]);
      close(1);
      n = dup(ipipe[1]);
      if(n!=1) exit(1);
      close(ipipe[1]);
      close(2);
      n = dup(1);
      if(n!=2) exit(1);

      execvp(PLAY_PROG,args);

      /* if exec returns, an error occured */
      exit(1);
   }

   /* Check if child sends right version number */

   vlen = strlen(LAVPLAY_VSTR);
   n = read(inp_pipe,version,vlen+1); /* vlen+1: for trailing \n */
   version[vlen] = 0;
   if(n!=vlen+1 || strncmp(version,LAVPLAY_VSTR,vlen)!=0)
   {
      fprintf(stderr,"%s did not send correct version info\n",PLAY_PROG);
      fprintf(stderr,"Got: \"%s\" Expected: \"%s\"\n",version,LAVPLAY_VERSION);
      do_real_exit(0,0);
   }
}

int main(int argc, char *argv[])
{
   int i;
   char **argvn;

   /* copy our argument list */

   argvn = (char**) malloc(sizeof(char*)*(argc+3));
   if(argvn==0) { fprintf(stderr,"malloc failed\n"); exit(1); }
   argvn[0] = PLAY_PROG;
   argvn[1] = "-q";
   argvn[2] = "-g";
   for(i=1;i<argc;i++) argvn[i+2] = argv[i];
   argvn[argc+2] = 0;

   create_child(argvn);

   gtk_init (&argc, &argv);
   gtk_xlav = create_form_xlav();
   gtk_signal_connect (GTK_OBJECT (gtk_xlav->xlav), "destroy",
       GTK_SIGNAL_FUNC(gtk_main_quit), NULL);
   gtk_widget_show(gtk_xlav->xlav); // show the main window

   gdk_input_add(inp_pipe,GDK_INPUT_READ,(GdkInputFunction)get_input,(gpointer)0);

   gtk_main();
   return 0;
}
