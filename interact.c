/* hexedit -- Hexadecimal Editor for Binary Files
   Copyright (C) 1998 Pixel (Pascal Rigaux)

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2, or (at your option)
   any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.*/
#include "hexedit.h"


static void goto_char(void);
static void goto_sector(void);
static void save_buffer(void);
static void escaped_command(void);
static void help(void);
static void short_help(void);
static void insert_string(void);
static void remove_marked(void);


/*******************************************************************************/
/* interactive functions */
/*******************************************************************************/

static void forward_char(void)
{
  if (!hexOrAscii || cursorOffset)
    move_cursor(+1);
  if (hexOrAscii) cursorOffset = (cursorOffset + 1) % 2;
}

static void backward_char(void)
{
  if (!hexOrAscii || !cursorOffset)
    move_cursor(-1);
  if (hexOrAscii) cursorOffset = (cursorOffset + 1) % 2;
}

static void next_line(void)
{
  move_cursor(+lineLength);
}

static void previous_line(void)
{
  move_cursor(-lineLength);
}

static void forward_chars(void)
{
  move_cursor(+blocSize);
}

static void backward_chars(void)
{
  move_cursor(-blocSize);
}

static void next_lines(void)
{
  move_cursor(+lineLength * blocSize);
}

static void previous_lines(void)
{
  move_cursor(-lineLength * blocSize);
}

static void beginning_of_line(void)
{
  cursorOffset = 0;
  move_cursor(-(cursor % lineLength));
}

static void end_of_line(void)
{
  cursorOffset = 0;
  if (!move_cursor(lineLength - 1 - cursor % lineLength))
    move_cursor(nbBytes - cursor);
}

static void scroll_up(void)
{
  move_base(+page);

  if (mark_set)
    updateMarked();
}

static void scroll_down(void)
{
  move_base(-page);

  if (mark_set)
    updateMarked();
}

static void beginning_of_buffer(void)
{
  cursorOffset = 0;
  set_cursor(0);
}

static void end_of_buffer(void)
{
  INT s = getfilesize();
  cursorOffset = 0;
  if (mode == bySector) set_base(myfloor(s, page));
  set_cursor(s);
}

static void suspend(void) { kill(getpid(), SIGTSTP); }
static void undo(void) { discardEdited(); readFile(); }
static void quoted_insert(void) { setTo(getch()); }
static void toggle(void) { hexOrAscii = (hexOrAscii + 1) % 2; }

static void recenter(void)
{
  if (cursor) {
    base = base + cursor;
    cursor = 0;
    readFile();
  }
}

static void find_file(void)
{
  if (!ask_about_save_and_redisplay()) return;
  if (!findFile()) { displayMessageAndWaitForKey("No such file or directory"); return; }
  openFile();
  readFile();
}

static void redisplay(void) { clear(); }

static void delete_backward_char(void)
{
  backward_char();
  removeFromEdited(base + cursor, 1);
  readFile();
  cursorOffset = 0;
  if (!tryloc(base + cursor)) end_of_buffer();
}

static void delete_backward_chars(void)
{
  backward_chars();
  removeFromEdited(base + cursor, blocSize);
  readFile();
  cursorOffset = 0;
  if (!tryloc(base + cursor)) end_of_buffer();
}

static void delete_forward_char(void) {
  removeFromEdited(base + cursor, 1);
  forward_char();
  forward_char();
  readFile();
  cursorOffset = 0;
  if (!tryloc(base + cursor)) end_of_buffer();
}

static void truncate_file(void)
{
  displayOneLineMessage("Really truncate here? (y/N)");
  if (tolower(getch()) == 'y') {
    if (biggestLoc > base+cursor && ftruncate(fd, base+cursor) == -1)
      displayMessageAndWaitForKey(strerror(errno));
    else {
      removeFromEdited(base+cursor, lastEditedLoc - (base+cursor));
      if (mark_set) {
        if (mark_min >= base + cursor || mark_max >= base + cursor)
          unmarkAll();
      }
      if (biggestLoc > base+cursor)
        biggestLoc = base+cursor;
      readFile();
    }
  }
}

static void firstTimeHelp(void)
{
  int firstTime = TRUE;

  if (firstTime) {
    firstTime = FALSE;
    short_help();
  }
}

static void set_mark_command(void)
{
  unmarkAll();
  if ((mark_set = not(mark_set))) {
    markIt(cursor);
    mark_min = mark_max = base + cursor;
  }
}


int setTo(int c)
{
  int val;

  if (cursor > nbBytes) return FALSE;
  if (hexOrAscii) {
      if (!isxdigit(c)) return FALSE;
      val = hexCharToInt(c);
      val = cursorOffset ? setLowBits(buffer[cursor], val) : setHighBits(buffer[cursor], val);
  }
  else val = c;

  if (isReadOnly) {
    displayMessageAndWaitForKey("File is read-only!");
  } else {
    setToChar(cursor, val);
    forward_char();
  }
  return TRUE;
}


/****************************************************
 ask_about_* or functions that present a prompt
****************************************************/


int ask_about_save(void)
{
  if (edited) {
    modeline_message("Save changes (Yes/No/Cancel) ?", 0);

    switch (tolower(getch()))
      {
      case 'y': save_buffer(); break;
      case 'n': discardEdited(); break;

      default:
        return FALSE;
      }
    return TRUE;
  }
  return -TRUE;
}

int ask_about_save_and_redisplay(void)
{
  int b = ask_about_save();
  if (b == TRUE) {
    readFile();
    display();
  }
  return b;
}

void ask_about_save_and_quit(void)
{
  if (ask_about_save()) quit();
}

static void goto_char(void)
{
  INT i;

  // displayOneLineMessage("New position ? ");
  modeline_message(":", 0);
  ungetstr("0x");
  if (!get_number(&i) || !set_cursor(i)) {
    modeline_message("Invalid position!", 0);
    getch();
  }
}

static void command_mode(void) {
  modeline_message(":", 0);
  char tmp[BLOCK_SEARCH_SIZE];
  echo();
  getnstr(tmp, BLOCK_SEARCH_SIZE - 1);
  noecho();
  int j = 0;
  for (; j < BLOCK_SEARCH_SIZE; j++) {
    if (tmp[j] == '\0') break;
    if (tmp[j] == 'w') {
      save_buffer();
    } else if (tmp[j] == 'q') {
      ask_about_save_and_quit();
    }
  }
}

static void goto_sector(void)
{
  INT i;

  displayOneLineMessage("New sector ? ");
  if (get_number(&i) && set_base(i * SECTOR_SIZE))
    set_cursor(i * SECTOR_SIZE);
  else
    displayMessageAndWaitForKey("Invalid sector!");
}



static void save_buffer(void)
{
  int displayedmessage = FALSE;
  typePage *p, *q;
  for (p = edited; p; p = q) {
    if (LSEEK_(fd, p->base) == -1 || write(fd, p->vals, p->size) == -1)
      if (!displayedmessage) {  /* It would be annoying to display lots of error messages when we can't write. */
        displayMessageAndWaitForKey(strerror(errno));
        displayedmessage = TRUE;
      }
    q = p->next;
    freePage(p);
  }
  edited = NULL;
  if (lastEditedLoc > fileSize) fileSize = lastEditedLoc;
  lastEditedLoc = 0;
  memset(bufferAttr, A_NORMAL, page * sizeof(*bufferAttr));
  if (displayedmessage) {
    displayMessageAndWaitForKey("Unwritten changes have been discarded");
    readFile();
    if (cursor > nbBytes) set_cursor(getfilesize());
  }
  if (mark_set) markSelectedRegion();
}

static void help(void)
{
  char *args[3];
  int status;

  args[0] = "man";
  args[1] = "hexedit";
  args[2] = NULL;
  endwin();
  if (fork() == 0) {
    execvp(args[0], args);
    exit(1);
  }
  wait(&status);
  refresh();
  raw();
}

static void short_help(void)
{
  modeline_message("Unknown command, press F1 for help", 0);
  getch();
}



/*******************************************************************************/
/* key_to_function */
/*******************************************************************************/
int key_to_function(int key)
{
  oldcursor = cursor;
  oldcursorOffset = cursorOffset;
  oldbase = base;
  /*printf("*******%d******\n", key);*/

  switch (key)
    {
    case 'l':
      if (!hexOrAscii) { goto SET_KEY; }
    case KEY_RIGHT:
      forward_char();
      break;

    case 'h':
      if (!hexOrAscii) { goto SET_KEY; }
    case KEY_LEFT:
      backward_char();
      break;

    case 'j':
      if (!hexOrAscii) { goto SET_KEY; }
    case '\n':
    case KEY_DOWN:
    case KEY_ENTER:
      next_line();
      break;

    case 'k':
      if (!hexOrAscii) { goto SET_KEY; }
    case KEY_UP:
      previous_line();
      break;

    case 'w':
      if (!hexOrAscii) { goto SET_KEY; }
      forward_chars();
      break;

    case 'W':
      if (!hexOrAscii) { goto SET_KEY; }
      backward_chars();
      break;

    case CTRL('D'):
      next_lines();
      break;

    case CTRL('U'):
      previous_lines();
      break;

    case '^':
      if (!hexOrAscii) { goto SET_KEY; }
    case '\r':
    case CTRL('A'):
    case KEY_HOME:
      beginning_of_line();
      break;

    case '$':
      if (!hexOrAscii) { goto SET_KEY; }
    case CTRL('E'):
    case KEY_END:
      end_of_line();
      break;

    case KEY_NPAGE:
    case CTRL('F'):
    case KEY_F(6):
      scroll_up();
      break;

    case KEY_PPAGE:
    case CTRL('B'):
    case KEY_F(5):
      scroll_down();
      break;

    case '<':
      if (!hexOrAscii) { goto SET_KEY; }
      beginning_of_buffer();
      break;

    case '>':
    case 'G':
      if (!hexOrAscii) { goto SET_KEY; }
      end_of_buffer();
      break;

    case KEY_SUSPEND:
    case CTRL('Z'):
      suspend();
      break;

    case 'u':
      if (!hexOrAscii) { goto SET_KEY; }
    case CTRL('_'):
      undo();
      break;

    case CTRL('Q'):
      quoted_insert();
      break;

    case '\t':
    case CTRL('T'):
      toggle();
      break;

    case '/':
      if (!hexOrAscii) { goto SET_KEY; }
      search_forward();
      break;

    case '?':
      if (!hexOrAscii) { goto SET_KEY; }
      search_backward();
      break;

    case 'g':
      if (!hexOrAscii) { goto SET_KEY; }
    case KEY_F(4):
      if (mode == bySector) goto_sector(); else goto_char();
      break;

    case ALT('L'):
      recenter();
      break;

    case CTRL('W'):
    case KEY_F(2):
      save_buffer();
      break;

    case CTRL('['): /* escape */
      escaped_command();
      break;

    case KEY_F(1):
      help();
      break;

    case KEY_F(3):
    case CTRL('O'):
      find_file();
      break;

    case CTRL('L'):
      redisplay();
      break;

    case CTRL('H'):
    case KEY_BACKSPACE:
    case 0x7F: /* found on a sun */
      delete_backward_char();
      break;

    case CTRL('H') | 0x80: /* CTRL-ALT-H */
      delete_backward_chars();
      break;

    case 'x':
      if (!hexOrAscii) { goto SET_KEY; }
    case KEY_DC:
      delete_forward_char();
      break;

    case 'v':
      if (!hexOrAscii) { goto SET_KEY; }
    case KEY_F(9):
      set_mark_command();
      break;

    case 'y':
      if (!hexOrAscii) { goto SET_KEY; }
    case KEY_F(7):
      copy_region();
      break;

    case 'p':
      if (!hexOrAscii) { goto SET_KEY; }
    case KEY_F(8):
      yank();
      break;

    case 'P':
      if (!hexOrAscii) { goto SET_KEY; }
    case KEY_F(11):
      yank_to_a_file();
      break;

    case ':':
      if (!hexOrAscii) { goto SET_KEY; }
      command_mode();
      break;

    case KEY_F(12):
      fill_with_string();
      break;

    case CTRL('C'):
      quit();
      break;

    case ALT('T'):
      truncate_file();
      break;

    case KEY_F(10):
    case CTRL('X'):
      ask_about_save_and_quit();
      break;

    case 'i':
      insert_string();
      break;

    case 'd':
      remove_marked();
      break;

    default:
SET_KEY:
      if ((key >= 256 || !setTo(key))) firstTimeHelp();
    }

  return TRUE;
}



static void escaped_command(void)
{
  char tmp[BLOCK_SEARCH_SIZE];
  int c, i;

  c = getch();
  switch (c)
  {
  case 'l':
    recenter();
    break;

  case 'h':
    help();
    break;

  default:
    firstTimeHelp();
  }
}

static void insert_string(void) {
  int res = ask_about_save();
  int base_bak = base, cursor_bak = cursor;
  if (res) {
    int tmp_size = 256;
    char tmp[tmp_size];
    char *msg = hexOrAscii ? "Hex string to insert: " : "Ascii string to insert: ";
    char **last = hexOrAscii ? &lastAskHexString : &lastAskAsciiString;
    displayMessageAndGetString(msg, last, tmp, tmp_size);
    int len = (int)strlen(tmp);
    size_t filesize = getfilesize();
    int i, j;
    if (hexOrAscii) if (!hexStringToBinString(tmp, &len)) return;
    for (i = filesize - 1; i >= base + cursor; i--) {
      LSEEK(fd, i);
      char tmpc;
      read(fd, &tmpc, 1);
      LSEEK(fd, i + len);
      write(fd, &tmpc, 1);
    }
    LSEEK(fd, base + cursor);
    write(fd, tmp, len);
    close(fd);
    openFile();
    readFile();
    set_base(base_bak);
    set_cursor(base_bak + cursor_bak);
    for (i = cursor; i < cursor + len; i++)
      bufferAttr[i] |= COLOR_PAIR(5);
  }
}

static void remove_marked(void) {
  int i, j;
  int mark_min = -1, mark_max = -1;
  int base_bak = base, cursor_bak = cursor;
  for (i = 0; i < page; i++) {
    if (bufferAttr[i] & MARKED) {
      if (mark_min < 0) mark_min = base + i;
      mark_max = base + i;
    } else if (mark_min > 0)
      break;
  }
  char c;
  for (i = mark_min, j = mark_max + 1; j < getfilesize(); i++, j++) {
    LSEEK(fd, j);
    read(fd, &c, 1);
    LSEEK(fd, i);
    write(fd, &c, 1);
  }
  ftruncate(fd, i);
  close(fd);
  openFile();
  readFile();
  set_base(base_bak);
  set_cursor(base_bak + cursor_bak);
}

/* vim: set et ai ts=2 sw=2 sts=2: */
