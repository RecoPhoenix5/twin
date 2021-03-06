/*
 *  util.c  --  utility functions
 *
 *  Copyright (C) 1993-2001 by Massimiliano Ghilardi
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 */

#include "twin.h"

#ifdef TW_HAVE_PWD_H
#include <pwd.h>
#endif
#ifdef TW_HAVE_UNISTD_H
#include <unistd.h>
#endif
#ifdef TW_HAVE_SIGNAL_H
#include <signal.h>
#endif
#ifdef TW_HAVE_SYS_SOCKET_H
#include <sys/socket.h>
#endif
#ifdef TW_HAVE_SYS_STAT_H
#include <sys/stat.h>
#endif
#ifdef TW_HAVE_SYS_TYPES_H
#include <sys/types.h>
#endif

#include "algo.h"
#include "alloc.h"
#include "data.h"
#include "extreg.h"
#include "methods.h"
#include "main.h"
#include "draw.h"
#include "remote.h"
#include "resize.h"
#include "printk.h"
#include "privilege.h"
#include "util.h"

#include "hw.h"

#include <Tw/Twkeys.h>
#include <Tutf/Tutf.h>

void NormalizeTime(timevalue *Time) {
  if (Time->Fraction >= FullSEC || Time->Fraction < 0) {
    tany delta = Time->Fraction / FullSEC;
    Time->Seconds += delta;
    Time->Fraction -= delta * FullSEC;
  }
}

timevalue *InstantNow(timevalue *Now) {
#if defined(TW_HAVE_GETTIMEOFDAY)
  struct timeval sysNow;

  gettimeofday(&sysNow, NULL);

  Now->Seconds = sysNow.tv_sec;
  Now->Fraction = sysNow.tv_usec MicroSECs;
#elif defined(TW_HAVE_FTIME)
  timeb sysNow;

  ftime(&sysNow);

  Now->Seconds = sysNow.time;
  Now->Fraction = sysNow.millitm MilliSECs;
#else
  Now->Seconds = time(NULL);
  Now->Fraction = 0;
#endif
  return Now;
}

timevalue *IncrTime(timevalue *Time, timevalue *Incr) {
  NormalizeTime(Time);
  NormalizeTime(Incr);

  Time->Seconds += Incr->Seconds;
  if ((Time->Fraction += Incr->Fraction) >= FullSEC) {
    Time->Seconds++;
    Time->Fraction -= FullSEC;
  }
  return Time;
}

timevalue *DecrTime(timevalue *Time, timevalue *Decr) {
  NormalizeTime(Time);
  NormalizeTime(Decr);

  Time->Seconds -= Decr->Seconds;
  if (Time->Fraction >= Decr->Fraction)
    Time->Fraction -= Decr->Fraction;
  else {
    Time->Seconds--;
    Time->Fraction += (FullSEC - Decr->Fraction);
  }
  return Time;
}

timevalue *SumTime(timevalue *Result, timevalue *Time, timevalue *Incr) {
  CopyMem(Time, Result, sizeof(timevalue));
  return IncrTime(Result, Incr);
}

timevalue *SubTime(timevalue *Result, timevalue *Time, timevalue *Decr) {
  CopyMem(Time, Result, sizeof(timevalue));
  return DecrTime(Result, Decr);
}

dat CmpTime(timevalue *T1, timevalue *T2) {
  NormalizeTime(T1);
  NormalizeTime(T2);

  if (T1->Seconds > T2->Seconds)
    return (dat)1;
  if (T1->Seconds < T2->Seconds)
    return (dat)-1;
  if (T1->Fraction > T2->Fraction)
    return (dat)1;
  if (T1->Fraction < T2->Fraction)
    return (dat)-1;
  return (dat)0;
}

static dat CmpCallTime(msgport m1, msgport m2) {
  if ((!m1->FirstMsg) != (!m2->FirstMsg))
    /* one of the two doesn't have msgs */
    return m1->FirstMsg ? (dat)-1 : (dat)1;
  if ((!m1->WakeUp) != (!m2->WakeUp))
    /* one doesn't need to be called */
    return m1->WakeUp ? (dat)-1 : (dat)1;
  if ((!m1->WakeUp) && (!m2->WakeUp))
    return 0;
  return CmpTime(&m1->CallTime, &m2->CallTime);
}

byte Minimum(byte MaxIndex, CONST ldat *Array) {
  byte i, MinIndex;
  ldat Temp;

  if (!MaxIndex)
    return 0xFF;

  Temp = Array[0];
  MinIndex = 0;

  for (i = 1; i < MaxIndex; i++) {
    if (Array[i] < Temp) {
      Temp = Array[i];
      MinIndex = i;
    }
  }
  return MinIndex;
}

/*
byte HexDigitToNum(byte HexDigit, byte *Error) {
     if (HexDigit>='0' && HexDigit<='9')
         return HexDigit - (byte)'0';

     if (HexDigit>='A' && HexDigit<='F')
         return (byte)10 + HexDigit - (byte)'A';

     if (HexDigit>='a' && HexDigit<='f')
         return (byte)10 + HexDigit - (byte)'a';

     *Error = ttrue;
     return (byte)0;
}

uldat HexStrToNum(byte *StringHex) {
     byte Len, Error = tfalse;
     uldat Value=(uldat)0;

     Len=(byte)strlen(StringHex);
     if (Len>(byte)8)
         return (uldat)0;

     do {
         Len--;
         Value |= (uldat)HexDigitToNum(StringHex[Len], &Error) << (Len << 2);
     }	while (!Error && Len>(byte)0);

    return Error ? (uldat)0 : Value;
}
*/

/* adapted from similar code in bdflush */
uldat ComputeUsableLenArgv(char *CONST *argv) {
  char *ptr;
  uldat count;

  ptr = argv[0] + strlen(argv[0]);
  for (count = 1; argv[count]; count++) {
    if (argv[count] == ptr + 1)
      ptr += strlen(ptr + 1) + 1;
  }
  return ptr - argv[0];
}

void SetArgv0(char *CONST *argv, uldat argv_usable_len, CONST char *src) {
  uldat len = strlen(src);

  if (len + 1 < argv_usable_len) {
    CopyMem(src, argv[0], len);
    memset(argv[0] + len, '\0', argv_usable_len - len);
  } else
    CopyMem(src, argv[0], argv_usable_len);
}

/*
 * move a msgport to the right place in an already sorted list,
 * ordering by CallTime
 */
void SortMsgPortByCallTime(msgport Port) {
  msgport other;
  if ((other = Port->Next) && CmpCallTime(Port, other) > 0) {
    Remove(Port);
    do {
      other = other->Next;
    } while (other && CmpCallTime(Port, other) > 0);
    if (other)
      InsertMiddle(MsgPort, Port, All, other->Prev, other);
    else
      InsertLast(MsgPort, Port, All);
  } else if ((other = Port->Prev) && CmpCallTime(Port, other) < 0) {
    Remove(Port);
    do {
      other = other->Prev;
    } while (other && CmpCallTime(Port, other) < 0);
    if (other)
      InsertMiddle(MsgPort, Port, All, other, other->Next);
    else
      InsertFirst(MsgPort, Port, All);
  }
}

/*
 * sort the msgport list by growing CallTime
 *
 * we use a bubble sort... no need to optimize to death this
 */
void SortAllMsgPortsByCallTime(void) {
  msgport Max, This, Port = All->FirstMsgPort;
  msgport Start, End;

  Start = End = (msgport)0;

  while (Port) {
    Max = This = Port;
    while ((This = This->Next)) {
      if (CmpCallTime(This, Max) > 0)
        Max = This;
    }
    if (Max == Port)
      /* careful, we are mucking the list under our feet */
      Port = Port->Next;

    Remove(Max);
    /*
     * HACK : we create a parentless list
     * backward, from End adding ->Prev until Start
     */
    Max->Next = Start;
    Max->All = All;
    if (Start)
      Start->Prev = Max;
    Start = Max;
    if (!End)
      End = Max;
  }
  All->FirstMsgPort = Start;
  All->LastMsgPort = End;
}

byte SendControlMsg(msgport MsgPort, udat Code, udat Len, CONST char *Data) {
  msg Msg;
  event_control *Event;

  if (MsgPort && (Msg = Do(Create, Msg)(FnMsg, MSG_CONTROL, Len))) {
    Event = &Msg->Event.EventControl;
    Event->Code = Code;
    Event->Len = Len;
    CopyMem(Data, Event->Data, Len);
    SendMsg(MsgPort, Msg);

    return ttrue;
  }
  return tfalse;
}

byte SelectionStore(uldat Magic, CONST char MIME[MAX_MIMELEN], uldat Len, CONST char *Data) {
  char *newData;
  selection *Sel = All->Selection;
  uldat newLen;
  byte pad;

  if (Magic == SEL_APPEND)
    newLen = Sel->Len + Len;
  else
    newLen = Len;

  if ((pad = (Sel->Magic == SEL_TRUNEMAGIC && (newLen & 1))))
    newLen++;

  if (Sel->Max < newLen) {
    if (!(newData = (char *)ReAllocMem(Sel->Data, newLen)))
      return tfalse;
    Sel->Data = newData;
    Sel->Max = newLen;
  }
  if (Magic != SEL_APPEND) {
    Sel->Owner = NULL;
    Sel->Len = 0;
    Sel->Magic = Magic;
    if (MIME)
      CopyMem(MIME, Sel->MIME, MAX_MIMELEN);
    else
      memset(Sel->MIME, '\0', MAX_MIMELEN);
  }
  if (Data)
    CopyMem(Data, Sel->Data + Sel->Len, Len);
  else
    memset(Sel->Data + Sel->Len, ' ', Len);
  Sel->Len += Len;
  if (pad) {
#if TW_IS_LITTLE_ENDIAN
    Sel->Data[Sel->Len++] = '\0';
#else
    Sel->Data[Sel->Len] = Sel->Data[Sel->Len - 1];
    Sel->Data[Sel->Len - 1] = '\0';
    Sel->Len++;
#endif
  }
  return ttrue;
}

#define _SEL_MAGIC SEL_TRUNEMAGIC
#if TW_IS_LITTLE_ENDIAN
#define _SelAppendNL() SelectionAppend(2, "\n\0");
#else
#define _SelAppendNL() SelectionAppend(2, "\0\n");
#endif

byte SetSelectionFromWindow(window Window) {
  ldat y;
  uldat slen, len;
  trune *sData, *Data;
  byte ok = ttrue, w_useC = W_USE(Window, USECONTENTS);

  if (!(Window->State & WINDOW_DO_SEL))
    return ok;

  if (!(Window->State & WINDOW_ANYSEL) || Window->YstSel > Window->YendSel ||
      (Window->YstSel == Window->YendSel && Window->XstSel > Window->XendSel)) {

    ok &= SelectionStore(_SEL_MAGIC, NULL, 0, NULL);
    if (ok)
      NeedHW |= NEEDSelectionExport;
    return ok;
  }

  /* normalize negative coords */
  if (Window->XstSel < 0)
    Window->XstSel = 0;
  else if (w_useC && Window->XstSel >= Window->WLogic) {
    Window->XstSel = 0;
    Window->YstSel++;
  }
  if (Window->YstSel < Window->YLogic) {
    Window->YstSel = Window->YLogic;
    Window->XstSel = 0;
  } else if (Window->YstSel >= Window->HLogic) {
    Window->YstSel = Window->HLogic - 1;
    Window->XstSel = w_useC ? Window->WLogic - 1 : TW_MAXLDAT;
  }

  if (w_useC) {
    tcell *hw;

    /* normalize negative coords */
    if (Window->XendSel < 0) {
      Window->XendSel = Window->WLogic - 1;
      Window->YendSel--;
    } else if (Window->XendSel >= Window->WLogic)
      Window->XendSel = Window->WLogic - 1;

    if (Window->YendSel < Window->YLogic) {
      Window->YendSel = Window->YLogic;
      Window->XendSel = 0;
    } else if (Window->YendSel >= Window->HLogic) {
      Window->YendSel = Window->HLogic - 1;
      Window->XendSel = Window->WLogic - 1;
    }

    if (!(sData = (trune *)AllocMem(sizeof(trune) * (slen = Window->WLogic))))
      return tfalse;

    hw = Window->USE.C.Contents + (Window->YstSel + Window->USE.C.HSplit) * slen;
    while (hw >= Window->USE.C.TtyData->Split)
      hw -= Window->USE.C.TtyData->Split - Window->USE.C.Contents;

    {
      y = Window->YstSel;
      if (y < Window->YendSel)
        slen -= Window->XstSel;
      else
        slen = Window->XendSel - Window->XstSel + 1;
      Data = sData;
      len = slen;
      hw += Window->XstSel;
      while (len--)
        *Data++ = TRUNE(*hw), hw++;
      ok &= SelectionStore(_SEL_MAGIC, NULL, slen * sizeof(trune), (CONST char *)sData);
    }

    if (hw >= Window->USE.C.TtyData->Split)
      hw -= Window->USE.C.TtyData->Split - Window->USE.C.Contents;

    slen = Window->WLogic;
    for (y = Window->YstSel + 1; ok && y < Window->YendSel; y++) {
      if (hw >= Window->USE.C.TtyData->Split)
        hw -= Window->USE.C.TtyData->Split - Window->USE.C.Contents;
      Data = sData;
      len = slen;
      while (len--)
        *Data++ = TRUNE(*hw), hw++;
      ok &= SelectionAppend(slen * sizeof(trune), (CONST char *)sData);
    }

    if (ok && Window->YendSel > Window->YstSel) {
      if (hw >= Window->USE.C.TtyData->Split)
        hw -= Window->USE.C.TtyData->Split - Window->USE.C.Contents;
      Data = sData;
      len = slen = Window->XendSel + 1;
      while (len--)
        *Data++ = TRUNE(*hw), hw++;
      ok &= SelectionAppend(slen * sizeof(trune), (CONST char *)sData);
    }
    if (ok)
      NeedHW |= NEEDSelectionExport;
    return ok;
  }
  if (W_USE(Window, USEROWS)) {
    row Row;

    /* Gap not supported! */
    y = Window->YstSel;
    Row = Act(FindRow, Window)(Window, y);

    if (Row && Row->Text) {
      if (y < Window->YendSel)
        slen = Row->Len - Window->XstSel;
      else
        slen = Min2(Row->Len, (uldat)Window->XendSel + 1) - Min2(Row->Len, (uldat)Window->XstSel);

      ok &= SelectionStore(_SEL_MAGIC, NULL, slen * sizeof(trune),
                           (CONST char *)(Row->Text + Min2(Row->Len, (uldat)Window->XstSel)));
    } else
      ok &= SelectionStore(_SEL_MAGIC, NULL, 0, NULL);

    if (y < Window->YendSel || !Row || !Row->Text || Row->Len <= (uldat)Window->XendSel)
      ok &= _SelAppendNL();

    for (y = Window->YstSel + 1; ok && y < Window->YendSel; y++) {
      if ((Row = Act(FindRow, Window)(Window, y)) && Row->Text)
        ok &= SelectionAppend(Row->Len * sizeof(trune), (CONST char *)Row->Text);
      ok &= _SelAppendNL();
    }
    if (Window->YendSel > Window->YstSel) {
      if (Window->XendSel >= 0 && (Row = Act(FindRow, Window)(Window, Window->YendSel)) &&
          Row->Text)
        ok &= SelectionAppend(Min2(Row->Len, (uldat)Window->XendSel + 1) * sizeof(trune),
                              (CONST char *)Row->Text);
      if (!Row || !Row->Text || Row->Len <= (uldat)Window->XendSel)
        ok &= _SelAppendNL();
    }
    if (ok)
      NeedHW |= NEEDSelectionExport;
    return ok;
  }
  return tfalse;
}

byte CreateXTermMouseEvent(event_mouse *Event, byte buflen, char *buf) {
  window W;
  udat Flags;
  udat Code = Event->Code;
  dat x = Event->X, y = Event->Y;
  byte len = 0;

  if (!(W = (window)Event->W) || !IS_WINDOW(W) || !W_USE(W, USECONTENTS) || !W->USE.C.TtyData)
    return len;

  Flags = W->USE.C.TtyData->Flags;

  if (Flags & TTY_REPORTMOUSE) {
    /* new-style reporting */

    /* when both TTY_REPORTMOUSE|TTY_REPORTMOUSE2 are set, also report motion */
    if (buflen < 9 || (Code == MOVE_MOUSE && !(Flags & TTY_REPORTMOUSE2)))
      /* buffer too small, or nothing to report */
      return len;

    /* report also button just pressed as down */
    if (isPRESS(Code))
      Code |= HOLD_CODE(PRESS_N(Code));

    CopyMem("\033[5M", buf, 4);
    buf[4] = ' ' + ((Code & HOLD_ANY) >> HOLD_BITSHIFT);
    buf[5] = '!' + (x & 0x7f);
    buf[6] = '!' + ((x >> 7) & 0x7f);
    buf[7] = '!' + (y & 0x7f);
    buf[8] = '!' + ((y >> 7) & 0x7f);
    len = 9;
  } else if (Flags & TTY_REPORTMOUSE2) {
    /* classic xterm-style reporting */

    if (buflen < 6)
      /* buffer too small! */
      return len;

    CopyMem("\033[M", buf, 3);

    if (isSINGLE_PRESS(Code))
      switch (Code & PRESS_ANY) {
      case PRESS_LEFT:
        buf[3] = ' ';
        break;
      case PRESS_MIDDLE:
        buf[3] = '!';
        break;
      case PRESS_RIGHT:
        buf[3] = '\"';
        break;
        /* WHEEL_REV and WHEEL_FWD supported only at release */
      }
    else if (isRELEASE(Code)) {
      switch (Code & RELEASE_ANY) {
#ifdef HOLD_WHEEL_REV
      case RELEASE_WHEEL_REV:
        buf[3] = '`';
        break;
#endif
#ifdef HOLD_WHEEL_FWD
      case RELEASE_WHEEL_FWD:
        buf[3] = 'a';
        break;
#endif
      default:
        buf[3] = '#';
        break;
      }
    } else
      return len;

    buf[4] = '!' + x;
    buf[5] = '!' + y;
    len = 6;
  }
  return len;
}

void ResetBorderPattern(void) {
  msgport MsgP;
  widget W;

  for (MsgP = All->FirstMsgPort; MsgP; MsgP = MsgP->Next) {
    for (W = MsgP->FirstW; W; W = W->O_Next) {
      if (IS_WINDOW(W))
        ((window)W)->BorderPattern[0] = ((window)W)->BorderPattern[1] = NULL;
    }
  }
}

static gadget _PrevGadget(gadget G) {
  while (G->Prev) {
    G = (gadget)G->Prev;
    if (IS_GADGET(G))
      return (gadget)G;
  }
  return (gadget)G->Prev;
}

static gadget _NextGadget(gadget G) {
  while (G->Next) {
    G = (gadget)G->Next;
    if (IS_GADGET(G))
      return (gadget)G;
  }
  return (gadget)G->Next;
}

/* handle common keyboard actions like cursor moving and button navigation */
void FallBackKeyAction(window W, event_keyboard *EventK) {
  ldat NumRow, OldNumRow;
  gadget G, H;

  if ((G = (gadget)W->SelectW) && IS_GADGET(G))
    switch (EventK->Code) {
    case TW_Escape:
      UnPressGadget(G, tfalse);
      W->SelectW = (widget)0;
      break;
    case TW_Return:
      UnPressGadget(G, ttrue);
      PressGadget(G);
      break;
    case TW_Up:
    case TW_Left:
      if ((H = _PrevGadget(G))) {
        if (!(G->Flags & GADGETFL_TOGGLE))
          UnPressGadget(G, tfalse);
        W->SelectW = (widget)H;
        PressGadget(H);
      }
      break;
    case TW_Down:
    case TW_Right:
    case TW_Tab:
      if ((H = _NextGadget(G))) {
        if (!(G->Flags & GADGETFL_TOGGLE))
          UnPressGadget(G, tfalse);
        W->SelectW = (widget)H;
        PressGadget(H);
      }
      break;
    default:
      break;
    }
  else if ((G = (gadget)W->FirstW) && IS_GADGET(G)) {
    PressGadget(G);
    W->SelectW = (widget)G;
  } else
    switch (EventK->Code) {
    case TW_Up:
      if (!W->HLogic)
        break;
      OldNumRow = W->CurY;
      if (OldNumRow < TW_MAXLDAT) {
        if (!OldNumRow)
          NumRow = W->HLogic - (ldat)1;
        else
          NumRow = OldNumRow - (ldat)1;
        W->CurY = NumRow;
        if (W->Flags & WINDOWFL_ROWS_SELCURRENT)
          DrawLogicWidget((widget)W, (ldat)0, OldNumRow, (ldat)TW_MAXDAT - (ldat)2, OldNumRow);
      } else
        W->CurY = NumRow = W->HLogic - (ldat)1;
      if (W->Flags & WINDOWFL_ROWS_SELCURRENT)
        DrawLogicWidget((widget)W, (ldat)0, NumRow, (ldat)TW_MAXDAT - (ldat)2, NumRow);
      UpdateCursor();
      break;
    case TW_Down:
      if (!W->HLogic)
        break;
      OldNumRow = W->CurY;
      if (OldNumRow < TW_MAXLDAT) {
        if (OldNumRow >= W->HLogic - (ldat)1)
          NumRow = (ldat)0;
        else
          NumRow = OldNumRow + (ldat)1;
        W->CurY = NumRow;
        if (W->Flags & WINDOWFL_ROWS_SELCURRENT)
          DrawLogicWidget((widget)W, (ldat)0, OldNumRow, (ldat)TW_MAXDAT - (ldat)2, OldNumRow);
      } else
        W->CurY = NumRow = (ldat)0;
      if (W->Flags & WINDOWFL_ROWS_SELCURRENT)
        DrawLogicWidget((widget)W, (ldat)0, NumRow, (ldat)TW_MAXDAT - (ldat)2, NumRow);
      UpdateCursor();
      break;
    case TW_Left:
      if (W->CurX > 0) {
        W->CurX--;
        UpdateCursor();
      }
      break;
    case TW_Right:
      if ((W_USE(W, USECONTENTS) && W->CurX < W->XWidth - 3) ||
          (W_USE(W, USEROWS) && W->CurX < TW_MAXLDAT - 1)) {
        W->CurX++;
        UpdateCursor();
      }
      break;
    default:
      break;
    }
}

/*
 * create a (malloced) array of non-space args
 * from arbitrary text command line
 *
 * FIXME: need proper handling of double quotes:
 * "a b" is the string `a b' NOT the two strings `"a' `b"'
 * (same for single quotes, backslashes, ...)
 */
char **TokenizeStringVec(uldat len, char *s) {
  char **cmd = NULL, *buf, c;
  uldat save_len, n = 0;

  /* skip initial spaces */
  while (len && ((c = *s) == '\0' || c == ' ')) {
    len--, s++;
  }
  save_len = len;

  if (len && (buf = (char *)AllocMem(len + 1))) {
    CopyMem(s, buf, len);
    buf[len] = '\0';

    /* how many args? */
    while (len) {
      len--, c = *s++;
      if (c && c != ' ') {
        n++;
        while (len && (c = *s) && c != ' ') {
          len--, s++;
        }
      }
    }
    if ((cmd = (char **)AllocMem((n + 1) * sizeof(char *)))) {
      n = 0;
      len = save_len;
      s = buf;

      /* put args in cmd[] */
      while (len) {
        len--, c = *s++;
        if (c && c != ' ') {
          cmd[n++] = s - 1;
          while (len && (c = *s) && c != ' ') {
            len--, s++;
          }
          *s = '\0'; /* safe, we did a malloc(len+1) */
        }
      }
      cmd[n] = NULL; /* safe, we did a malloc(n+1) */
    }
  }
  return cmd;
}

void FreeStringVec(char **cmd) {
  if (cmd) {
    FreeMem(cmd[0]);
    FreeMem(cmd);
  }
}

/*
 * create a (malloced) array of non-space args
 * from arbitrary text command line
 *
 * FIXME: need proper handling of double quotes:
 * "a b" is the string `a b' NOT the two strings `"a' `b"'
 * (same for single quotes, backslashes, ...)
 */
char **TokenizeTRuneVec(uldat len, trune *s) {
  char **cmd = NULL, *buf, *v;
  trune c;
  uldat save_len, n = 0, i;

  /* skip initial spaces */
  while (len && ((c = *s) == '\0' || c == ' ')) {
    len--, s++;
  }
  save_len = len;

  if (len && (buf = (char *)AllocMem(len + 1))) {
    for (i = 0; i < len; i++)
      buf[i] = s[i];
    buf[len] = '\0';

    /* how many args? */
    while (len) {
      len--, c = *s++;
      if (c && c != ' ') {
        n++;
        while (len && (c = *s) && c != ' ') {
          len--, s++;
        }
      }
    }
    if ((cmd = (char **)AllocMem((n + 1) * sizeof(char *)))) {
      n = 0;
      len = save_len;
      v = buf;

      /* put args in cmd[] */
      while (len) {
        len--, c = *v++;
        if (c && c != ' ') {
          cmd[n++] = v - 1;
          while (len && (c = *v) && c != ' ') {
            len--, v++;
          }
          *v = '\0'; /* safe, we did a malloc(len+1) */
        }
      }
      cmd[n] = NULL; /* safe, we did a malloc(n+1) */
    }
  }
  return cmd;
}

int unixFd;
uldat unixSlot;

static void TWDisplayIO(int fd, uldat slot) {
  struct sockaddr_un un_addr;
  socklen_t len = sizeof(un_addr);

  if ((fd = accept(fd, (struct sockaddr *)&un_addr, &len)) >= 0) {
    close(fd);
  }
}

static char envTWD[] = "TWDISPLAY=\0\0\0\0\0";

CONST char *TmpDir(void) {
  CONST char *tmp = getenv("TMPDIR");
  if (tmp == NULL)
    tmp = "/tmp";
  return tmp;
}

udat CopyToSockaddrUn(CONST char *src, struct sockaddr_un *addr, udat pos) {
  size_t len = strlen(src), max = sizeof(addr->sun_path) - 1; /* for final '\0' */
  if (pos < max) {
    if (len >= max - pos)
      len = max - pos - 1;
    CopyMem(src, addr->sun_path + pos, len);
    addr->sun_path[pos += len] = '\0';
  }
  return pos;
}

static struct sockaddr_un addr;
static CONST char *fullTWD = addr.sun_path;
static char twd[12];

/* set TWDISPLAY and create /tmp/.Twin:<x> */
byte InitTWDisplay(void) {
  char *arg0;
  int fd = NOFD;
  unsigned short i;
  udat len;
  byte ok;

  HOME = getenv("HOME");
  memset(&addr, 0, sizeof(addr));

  if ((unixFd = socket(AF_UNIX, SOCK_STREAM, 0)) >= 0) {

    addr.sun_family = AF_UNIX;

    for (i = 0; i < 0x1000; i++) {
      sprintf(twd, ":%hx", i);

      len = CopyToSockaddrUn(TmpDir(), &addr, 0);
      len = CopyToSockaddrUn("/.Twin", &addr, len);
      len = CopyToSockaddrUn(twd, &addr, len);

      ok = bind(unixFd, (struct sockaddr *)&addr, sizeof(addr)) >= 0;
      if (!ok) {
        Error(SYSCALLERROR);
        /* maybe /tmp/.Twin:<x> is already in use... */
        if (fd >= 0 || (fd = socket(AF_UNIX, SOCK_STREAM, 0)) >= 0) {
          if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) >= 0) {
            /*
             * server is alive, try to grab another TWDISPLAY.
             * also, we must junk `fd' since SOCK_STREAM sockets
             * can connect() only once
             */
            close(fd);
            fd = NOFD;
            continue;
          }
          /* either server is dead or is owned by another user */
          if (unlink(fullTWD) >= 0) {
            /*
             * if we have permission to delete it, we also had
             * the permission to connect to it (hope).
             * So it must have been be a stale socket.
             *
             * Trying to delete a /tmp/.Twin:<x> entry we cannot
             * connect to wreaks havoc if you mix this twin server
             * with older ones, but having two different servers
             * installed on one system should be rare enough.
             */
            ok = bind(unixFd, (struct sockaddr *)&addr, sizeof(addr)) >= 0;
          }
        }
      }
      if (ok) {
        if (chmod(fullTWD, 0700) >= 0 && listen(unixFd, 3) >= 0 &&
            fcntl(unixFd, F_SETFD, FD_CLOEXEC) >= 0) {

          if ((unixSlot = RegisterRemoteFd(unixFd, TWDisplayIO)) != NOSLOT) {

            if (fd != NOFD)
              close(fd);
            TWDisplay = twd;
            lenTWDisplay = strlen(TWDisplay);
            CopyMem(TWDisplay, envTWD + 10, lenTWDisplay);
#if defined(TW_HAVE_SETENV)
            setenv("TWDISPLAY", TWDisplay, 1);
            setenv("TERM", "linux", 1);
#elif defined(TW_HAVE_PUTENV)
            putenv(envTWD);
            putenv("TERM=linux");
#endif
            if ((arg0 = (char *)AllocMem(strlen(TWDisplay) + 6))) {
              sprintf(arg0, "twin %s", TWDisplay);
              SetArgv0(main_argv, main_argv_usable_len, arg0);
              FreeMem(arg0);
            }
            return ttrue;
          }
        } else
          Error(SYSCALLERROR);
        close(unixFd);
      }
    }
  }
  if (fd != NOFD)
    close(fd);

  CopyToSockaddrUn(TmpDir(), &addr, 0);
  arg0 = addr.sun_path;

  printk("twin: failed to create any " SS "/.Twin* socket: " SS "\n", addr.sun_path, ErrStr);
  printk("      possible reasons: either " SS " not writable, or all TWDISPLAY already in use,\n"
         "      or too many stale " SS "/.Twin* sockets. Aborting.\n",
         arg0, arg0);
  return tfalse;
}

/* unlink /tmp/.Twin<TWDISPLAY> */
void QuitTWDisplay(void) {
  unlink(fullTWD);
}

static e_privilege Privilege;
static uid_t Uid, EUid;
static gid_t tty_grgid;

byte CheckPrivileges(void) {
  Uid = getuid();
  EUid = geteuid();
  tty_grgid = get_tty_grgid();

  if (GainRootPrivileges() >= 0)
    Privilege = suidroot;
  else if (tty_grgid != (gid_t)-1 && GainGroupPrivileges(tty_grgid) >= 0)
    Privilege = sgidtty;
  else
    Privilege = none;

  DropPrivileges();

  return Privilege;
}

void GainPrivileges(void) {
  if (Privilege == suidroot)
    GainRootPrivileges();
  else if (Privilege == sgidtty)
    GainGroupPrivileges(get_tty_grgid());
}

static void SetEnvs(struct passwd *p) {
  char buf[TW_BIGBUFF];

  chdir(HOME = p->pw_dir);
#if defined(TW_HAVE_SETENV)
  setenv("HOME", HOME, 1);
  setenv("SHELL", p->pw_shell, 1);
  setenv("LOGNAME", p->pw_name, 1);
  sprintf(buf, "/var/mail/%.*s", (int)(TW_BIGBUFF - 11), p->pw_name);
  setenv("MAIL", buf, 1);
#elif defined(TW_HAVE_PUTENV)
  sprintf(buf, "HOME=%.*s", (int)(TW_BIGBUFF - 6), HOME);
  putenv(buf);
  sprintf(buf, "SHELL=%.*s", (int)(TW_BIGBUFF - 7), p->pw_shell);
  putenv(buf);
  sprintf(buf, "LOGNAME=%.*s", (int)(TW_BIGBUFF - 9), p->pw_name);
  putenv(buf);
  sprintf(buf, "MAIL=/var/mail/%.*s", (int)(TW_BIGBUFF - 16) p->pw_name);
  putenv(buf);
#endif
}

byte SetServerUid(uldat uid, byte privileges) {
  msgport WM_MsgPort;
  struct passwd *p;
  byte ok = tfalse;

  if (flag_secure && uid == (uldat)(uid_t)uid && Uid == 0 && EUid == 0) {
    if ((WM_MsgPort = Ext(WM, MsgPort))) {
      if ((p = getpwuid(uid)) && p->pw_uid == uid && chown(fullTWD, p->pw_uid, p->pw_gid) >= 0
#ifdef TW_HAVE_INITGROUPS
          && init_groups(p->pw_name, p->pw_gid) >= 0
#endif
      ) {

        switch (privileges) {
        case none:
          ok = setgid(p->pw_gid) >= 0 && setuid(uid) >= 0;
          break;
        case sgidtty:
          ok = setregid(p->pw_gid, tty_grgid) >= 0 && setuid(uid) >= 0;
          break;
        case suidroot:
          ok = setgid(p->pw_gid) >= 0 && setreuid(uid, 0) >= 0;
          break;
        default:
          break;
        }
        if (ok && (uid == 0 || CheckPrivileges() == privileges)) {
          flag_secure = 0;
          SetEnvs(p);
          if ((ok = Ext(Socket, InitAuth)())) {
            /*
             * it's time to execute .twenvrc.sh and read its output to set
             * environment variables (mostly useful for twdm)
             */
            RunTwEnvRC();

            /* tell the WM to restart itself (so that it reads user's .twinrc) */
            SendControlMsg(WM_MsgPort, MSG_CONTROL_OPEN, 0, NULL);
            return ttrue;
          }
        } else
          ok = tfalse;

        if (!ok) {
          flag_secure = 1;
          if (setuid(0) < 0 || setgid(0) < 0 || chown(fullTWD, 0, 0) < 0) {
            /* tried to recover, but screwed up uids too badly. */
            printk("twin: failed switching to uid %u: " SS "\n", uid, strerror(errno));
            printk("twin: also failed to recover. Quitting NOW!\n");
            Quit(0);
          }
          SetEnvs(getpwuid(0));
        }
      }
      printk("twin: failed switching to uid %u: " SS "\n", uid, strerror(errno));
    }
  } else
    printk("twin: SetServerUid() can be called only if started by root with \"-secure\".\n");
  return tfalse;
}

/*
 * search for a file relative to HOME, to PKG_LIBDIR or as path
 *
 * this for example will search "foo"
 * as "${HOME}/foo", "${PKG_LIBDIR}/system.foo" or plain "foo"
 */
char *FindFile(CONST char *name, uldat *fsize) {
  CONST char *prefix[3], *infix[3];
  char *path;
  CONST char *dir;
  int i, min_i, max_i, len, nlen = strlen(name);
  struct stat buf;

  prefix[0] = HOME;
  infix[0] = (HOME && *HOME) ? "/" : "";
  prefix[1] = pkg_libdir;
  infix[1] = "/system";
  prefix[2] = "";
  infix[2] = "";

  if (flag_secure)
    min_i = max_i = 1; /* only pkg_libdir */
  else
    min_i = 0, max_i = 2;

  for (i = min_i; i <= max_i; i++) {
    if (!(dir = prefix[i]))
      continue;
    len = strlen(dir) + strlen(infix[i]);
    if ((path = (char *)AllocMem(len + nlen + 2))) {
      sprintf(path, "%s%s%s", dir, infix[i], name);
      if (stat(path, &buf) == 0) {
        if (fsize)
          *fsize = buf.st_size;
        return path;
      }
      FreeMem(path);
    }
  }
  return NULL;
}

/*
 * read data from infd and set environment variables accordingly
 */
static void ReadTwEnvRC(int infd) {
  char buff[TW_BIGBUFF], *p = buff, *end, *q, *eq;
  int got, left = TW_BIGBUFF;
  for (;;) {
    do {
      got = read(infd, p, left);
    } while (got == -1 && errno == EINTR);

    if (got <= 0)
      break;

    end = p + got;
    p = buff;

    while ((eq = (char *)memchr(p, '=', end - p)) && (q = (char *)memchr(eq, '\n', end - eq))) {

      *q++ = '\0';
#if defined(TW_HAVE_SETENV)
      *eq++ = '\0';
      setenv(p, eq, 1);
#elif defined(TW_HAVE_PUTENV)
      putenv(p);
#endif
      p = q;
    }
    left = end - p;
    if (left == TW_BIGBUFF)
      /* line too long! */
      left = 0;

    memmove(buff, p, left);
    p = buff + left;
    left = TW_BIGBUFF - left;
  }
}

/*
 * execute .twenvrc.sh <dummy> and read its output to set
 * environment variables (mostly useful for twdm)
 */
void RunTwEnvRC(void) {
  char *path;
  uldat len;
  int fds[2];

  if (flag_envrc != 1)
    return;

  if (flag_secure == 0) {
    flag_envrc = 0;

    if ((path = FindFile(".twenvrc.sh", &len))) {
      if ((pipe(fds) >= 0)) {
        switch (fork()) {
        case -1: /* error */
          close(fds[0]);
          close(fds[1]);
          printk("twin: RunTwEnvRC(): fork() failed: " SS "\n", strerror(errno));
          break;
        case 0: /* child */
          close(fds[0]);
          if (fds[1] != 2) {
            close(2);
            dup2(fds[1], 2);
            close(fds[1]);
          }
          close(1);
          dup2(0, 1);
          execl(path, path, "dummy", NULL);
          exit(0);
          break;
        default: /* parent */
          close(fds[1]);
          ReadTwEnvRC(fds[0]);
          close(fds[0]);
          break;
        }
      } else
        printk("twin: RunTwEnvRC(): pipe() failed: " SS "\n", strerror(errno));
    } else
      printk("twin: RunTwEnvRC(): .twenvrc.sh: File not found\n", strerror(errno));
  } else
    printk("twin: RunTwEnvRC(): delaying .twenvrc.sh execution until secure mode ends.\n");
}

/* remove CONST from a pointer and suppress compiler warnings */
void *RemoveConst(CONST void *x) {
  union {
    CONST void *cv;
    void *v;
  } u = {x};
  return u.v;
}

/*
 * encode POS_* position, position detail, active flag, pressed flag,
 * into 'extra' byte field inside tcell
 *
 * not all bits are preserved... this is just
 * a fair effort that covers most cases
 *
 * this is used to decide which pseudo-graphic cell to load
 * from hw_gfx_themes/<*>.xpm theme files.
 */
tcell EncodeToTCellExtra(tpos pos, tternary detail, tbool active, tbool pressed) {
  enum { pitch = 15 };
  byte o12 = active ? pressed ? 2 : 1 : 0;
  byte sides = (4 + o12) * pitch;
  byte scrollx = 9 + (4 + o12) * pitch;
  byte scrolly = 12 + o12;

  switch (pos) {
  case POS_TITLE:
    return 4 + sides;
  case POS_SIDE_LEFT:
    return 3 + sides;
  case POS_SIDE_UP:
    return detail + sides;
  case POS_SIDE_RIGHT:
    return 5 + sides;
  case POS_SIDE_DOWN:
    return 6 + detail + sides;

  case POS_BUTTON_RESIZE:
    return (detail ? 1 : 0) + (pressed ? 3 : 2) * pitch;

  case POS_X_BAR_BACK:
  case POS_X_BAR_FWD:
    return scrollx;
  case POS_X_TAB:
    return 1 + scrollx;
  case POS_X_ARROW_BACK:
    return 2 + scrollx;
  case POS_X_ARROW_FWD:
    return 3 + scrollx;

  case POS_Y_BAR_BACK:
  case POS_Y_BAR_FWD:
    return scrolly;
  case POS_Y_TAB:
    return scrolly + pitch;
  case POS_Y_ARROW_BACK:
    return scrolly + 2 * pitch;
  case POS_Y_ARROW_FWD:
    return scrolly + 3 * pitch;
  case POS_INSIDE:
    return 1 + pitch;
  case POS_MENU:
    return 1;
  case POS_ROOT:
    return pitch;
  default:
    if (pos < POS_TITLE)
      return 2 + pos * 2 + (detail ? 1 : 0) + ((pressed ? 1 : 0) + (pos >= 5)) * pitch;
    break;
  }
  return 0;
}
