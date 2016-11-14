/******************************************************************************\
* Copyright (c) 2016, Robert van Engelen, Genivia Inc. All rights reserved.    *
*                                                                              *
* Redistribution and use in source and binary forms, with or without           *
* modification, are permitted provided that the following conditions are met:  *
*                                                                              *
*   (1) Redistributions of source code must retain the above copyright notice, *
*       this list of conditions and the following disclaimer.                  *
*                                                                              *
*   (2) Redistributions in binary form must reproduce the above copyright      *
*       notice, this list of conditions and the following disclaimer in the    *
*       documentation and/or other materials provided with the distribution.   *
*                                                                              *
*   (3) The name of the author may not be used to endorse or promote products  *
*       derived from this software without specific prior written permission.  *
*                                                                              *
* THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR IMPLIED *
* WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF         *
* MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO   *
* EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,       *
* SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, *
* PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;  *
* OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,     *
* WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR      *
* OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF       *
* ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.                                   *
\******************************************************************************/

/**
@file      pattern.h
@brief     RE/flex regular expression pattern compiler
@author    Robert van Engelen - engelen@genivia.com
@copyright (c) 2015-2016, Robert van Engelen, Genivia Inc. All rights reserved.
@copyright (c) BSD-3 License - see LICENSE.txt
*/

#ifndef REFLEX_PATTERN_H
#define REFLEX_PATTERN_H

#include "bits.h"
#include "debug.h"
#include "input.h"
#include "ranges.h"
#include "setop.h"
#include <iostream>
#include <iomanip>
#include <string>
#include <list>
#include <map>
#include <set>
#include <vector>

namespace reflex {

/**
ReFlex pattern class holds a regex pattern and its compiled finite state
machine and code for the reflex::Matcher engine.

TODO
*/
class Pattern {
  friend class Matcher; ///< permit access by the reflex::Matcher engine
 public:
  /// Throwable error structure reflex::Pattern::Error.
  struct Error {
    /// Error codes raised during Pattern object construction.
    enum Code {
      REGEX_SYNTAX,   ///< regex syntax error
      REGEX_RANGE,    ///< regex `{n,m}` range error
      REGEX_LIST,     ///< regex `[a-b]` list error
      CODE_OVERFLOW   ///< compiled code is too large
    };
    /// Construct error structure
    Error(
        const Pattern *pattern, ///< points to the pattern object with this error
        enum Code      code,    ///< the error code
        const char    *message, ///< a human-readable error message (not NULL)
        size_t         loc)     ///< location of the error in regex string Pattern::rex_ or 0
      :
        pattern(*pattern),
        code(code),
        message(message),
        loc(loc)
    { }
    void display(std::ostream& os = std::cerr) const;
    const Pattern& pattern; ///< the pattern object with this error
    enum Code      code;    ///< the error code
    const char    *message; ///< a human-readable error message (not NULL)
    size_t         loc;     ///< location of the error in regex string Pattern::rex_ or 0
  };
  typedef uint16_t Index;  ///< index into opcodes array Pattern::opc_ and subpattern indexing
  typedef uint32_t Opcode; ///< 32 bit opcode word
  /// Common constants.
  enum Const {
    NPOS = std::string::npos, ///< alias for `std::string::npos`
    IMAX = 0xffff,            ///< max index, also serves as a marker
  };
  /// Construct a pattern object given a regex string.
  Pattern(
      const char *regex,
      const char *options = NULL) throw (Error)
    :
      rex_(regex),
      opc_(NULL)
  {
    init(options);
  }
  /// Construct a pattern object given a regex string.
  explicit Pattern(
      const char        *regex,
      const std::string& options) throw (Error)
    :
      rex_(regex),
      opc_(NULL)
  {
    init(options.c_str());
  }
  /// Construct a pattern object given a regex string.
  explicit Pattern(
      const std::string& regex,
      const char        *options = NULL) throw (Error)
    :
      rex_(regex),
      opc_(NULL)
  {
    init(options);
  }
  /// Construct a pattern object given a regex string.
  explicit Pattern(
      const std::string& regex,
      const std::string& options) throw (Error)
    :
      rex_(regex),
      opc_(NULL)
  {
    init(options.c_str());
  }
  /// Construct a pattern object given a code array.
  explicit Pattern(const Opcode *code) throw (Error)
    :
      opc_(code)
  {
    init(NULL);
  }
  /// Destructor, deletes internal code array when owned and allocated.
  virtual ~Pattern(void)
  {
    if (nop_ && opc_)
      delete[] opc_;
  }
  /// Number of subpatterns of this pattern object.
  /// @returns number of subpatterns.
  Index size(void) const
  {
    return end_.size();
  }
  /// Get subpattern of this pattern object.
  /// @returns subpattern string or "".
  const std::string operator[](Index choice) const;
  /// Check is subpattern is reachable by a match.
  /// @returns true if subpattern is reachable.
  bool reachable(Index choice) const
  {
    return choice >= 1 && choice <= size() && acc_.at(choice - 1);
  }
  /// Get the number of finite state machine nodes (vertices).
  /// @returns number of nodes or 0 when no finite state machine was constructed by this pattern.
  size_t nodes(void) const
  {
    return nop_ ? vno_ : 0;
  }
  /// Get the number of finite state machine edges (transitions on input characters).
  /// @returns number of edges or 0 when no finite state machine was constructed by this pattern.
  size_t edges(void) const
  {
    return nop_ ? eno_ : 0;
  }
  /// Get the code size in number of words.
  /// @returns number of words or 0 when no code was generated by this pattern.
  size_t words(void) const
  {
    return nop_;
  }
 protected:
  /// Throw an error.
  virtual void error(
      enum Error::Code code,    ///< error code
      const char      *message, ///< a human-readable error message (not NULL)
      size_t           loc = 0) ///< optional location of the error in regex string Pattern::rex_
    const throw (Error);
 private:
  typedef unsigned int         Char;
#ifdef BITS
  typedef Bits                 Chars; // represent 8-bit char (+ meta char) set as a bitvector
#else
  typedef ORanges<Char>        Chars; // represent (wide) char set as a set of ranges
#endif
  typedef size_t               Location;
  typedef ORanges<Location>    Ranges;
  typedef std::set<Location>   Set;
  typedef std::map<int,Ranges> Map;
  struct Position {
    typedef uint64_t           value_type;
    static const value_type    NPOS = static_cast<value_type>(-1LL);
    static const value_type    TICKED = 1LL << 44;
    static const value_type    GREEDY = 1LL << 45;
    static const value_type    ANCHOR = 1LL << 46;
    static const value_type    ACCEPT = 1LL << 47;
    Position()                 : k(NPOS) { }
    Position(value_type k)     : k(k) { }
    Position(const Position& p): k(p.k) { }
    operator value_type()      const { return k; }
    Position iter(Index i)     const { return Position(k | static_cast<value_type>(i) << 16); }
    Position ticked(bool b)    const { return b ? Position(k | TICKED) : Position(k & ~TICKED); }
    Position greedy(bool b)    const { return b ? Position(k | GREEDY) : Position(k & ~GREEDY); }
    Position anchor(bool b)    const { return b ? Position(k | ANCHOR) : Position(k & ~ANCHOR); }
    Position accept(bool b)    const { return b ? Position(k | ACCEPT) : Position(k & ~ACCEPT); }
    Position lazy(Location l)  const { return Position((k & 0x0000ffffffffffffLL) | static_cast<value_type>(l) << 48); }
    Position pos(void)         const { return Position(k & 0x00000000ffffffffLL); }
    Location loc(void)         const { return k & 0xffff; }
    Index    accepts(void)     const { return k & 0xffff; }
    Index    iter(void)        const { return k >> 16 & 0xffff; }
    bool     ticked(void)      const { return k & TICKED; }
    bool     greedy(void)      const { return k & GREEDY; }
    bool     anchor(void)      const { return k & ANCHOR; }
    bool     accept(void)      const { return k & ACCEPT; }
    Location lazy(void)        const { return k >> 48 & 0xffff; }
    value_type k;
  };
  typedef std::set<Position>           Positions;
  typedef std::map<Position,Positions> Follow;
  typedef std::pair<Chars,Positions>   Move;
  typedef std::list<Move>              Moves;
  /// Finite state machine.
  struct State : Positions {
    typedef std::map<Char,std::pair<Char,State*> > Edges;
    State(const Positions& p)
      :
        Positions(p),
        index(0),
        accept(0),
        redo(false),
        next(NULL),
        left(NULL),
        right(NULL)
    { }
    // Positions positions;
    Edges  edges;
    Index  index;  ///< index of this state
    Index  accept; ///< nonzero if final state, the index of an accepted/captured subpattern
    bool   redo;   ///< true if this is an ignorable final state
    Set    heads;  ///< lookahead head set
    Set    tails;  ///< lookahead tail set
    State *next;   ///< points to sibling state allocated depth-first by subset construction
    State *left;   ///< left pointer for O(log N) node insertion in the state graph
    State *right;  ///< right pointer for O(log N) node insertion in the state graph
  };
  /// Global modifier modes, syntax flags, and compiler options.
  struct Option {
    bool                     b; ///< disable escapes in bracket lists
    Char                     e; ///< escape character, or '\0' for none, '\\' default
    std::vector<std::string> f; ///< output to files
    bool                     i; ///< case insensitive mode, also `(?i:X)`
    bool                     l; ///< lex mode
    bool                     m; ///< multi-line mode, also `(?m:X)`
    std::string              n; ///< pattern name (for use in generated code)
    bool                     q; ///< enable "X" quotation of verbatim content, also `(?q:X)`
    bool                     r; ///< raise syntax errors
    bool                     s; ///< single-line mode (dotall mode), also `(?s:X)`
    bool                     w; ///< write error message to stderr
    bool                     x; ///< free-spacing mode, also `(?x:X)`
  };
  /// Meta characters.
  enum Meta {
    META_MIN = 0x100,
    META_NWB = 0x101, ///< non-word at begin      `\Bx`
    META_NWE = 0x102, ///< non-word at end        `x\B`
    META_BWB = 0x103, ///< begin of word at begin `\<x`
    META_EWB = 0x104, ///< end of word at begin   `\>x`
    META_BWE = 0x105, ///< begin of word at end   `x\<`
    META_EWE = 0x106, ///< end of word at end     `x\>`
    META_BOL = 0x107, ///< begin of line          `^`
    META_EOL = 0x108, ///< end of line            `$`
    META_BOB = 0x109, ///< begin of buffer        `\A`
    META_EOB = 0x10a, ///< end of buffer          `\Z`
    META_IND = 0x10b, ///< indent boundary        `\i`
    META_DED = 0x10c, ///< dedent boundary        `\j` (must be the largest META code)
    META_MAX          ///< max meta characters
  };
  /// Initialize the pattern at construction.
  void init(const char *options) throw (Error);
  void init_options(const char *options);
  void parse(
      Positions& startpos,
      Follow&    followpos,
      Map&       modifiers,
      Map&       lookahead) throw (Error);
  void parse1(
      bool       begin,
      Location&  loc,
      Positions& firstpos,
      Positions& lastpos,
      bool&      nullable,
      Follow&    followpos,
      Positions& lazypos,
      Map&       modifiers,
      Ranges&    lookahead,
      Index&     iter) throw (Error);
  void parse2(
      bool       begin,
      Location&  loc,
      Positions& firstpos,
      Positions& lastpos,
      bool&      nullable,
      Follow&    followpos,
      Positions& lazypos,
      Map&       modifiers,
      Ranges&    lookahead,
      Index&     iter) throw (Error);
  void parse3(
      bool       begin,
      Location&  loc,
      Positions& firstpos,
      Positions& lastpos,
      bool&      nullable,
      Follow&    followpos,
      Positions& lazypos,
      Map&       modifiers,
      Ranges&    lookahead,
      Index&     iter) throw (Error);
  void parse4(
      bool       begin,
      Location&  loc,
      Positions& firstpos,
      Positions& lastpos,
      bool&      nullable,
      Follow&    followpos,
      Positions& lazypos,
      Map&       modifiers,
      Ranges&    lookahead,
      Index&     iter) throw (Error);
  void parse_esc(Location& loc) const throw (Error);
  void compile(
      State&     start,
      Follow&    followpos,
      const Map& modifiers,
      const Map& lookahead) throw (Error);
  void lazy(
      const Positions& lazypos,
      Positions&       pos) const;
  void lazy(
      const Positions& lazypos,
      const Positions& pos,
      Positions&       pos1) const;
  void greedy(Positions& pos) const;
  void trim_lazy(Positions& pos) const;
  void compile_transition(
      State     *state,
      Follow&    followpos,
      const Map& modifiers,
      const Map& lookahead,
      Moves&     moves) const throw (Error);
  void transition(
      Moves&           moves,
      const Chars&     chars,
      const Positions& follow) const;
  Char compile_esc(
      Location loc,
      Chars&   chars) const throw (Error);
  void compile_list(
      Location   loc,
      Chars&     chars,
      const Map& modifiers) const throw (Error);
  void posix(
      size_t index,
      Chars& chars) const;
  void flip(Chars& chars) const;
  void assemble(State& start) throw (Error);
  void compact_dfa(State& start);
  void encode_dfa(State& start) throw (Error);
  void delete_dfa(State& start);
  void export_dfa(const State& start) const;
  void export_code(void) const;
  Location find_at(
      Location loc,
      char     c) const
  {
    return rex_.find_first_of(c, loc);
  }
  Char at(Location k) const
  {
    return static_cast<unsigned char>(rex_[k]);
  }
  bool eq_at(
      Location    loc,
      const char *s) const
  {
    return rex_.compare(loc, strlen(s), s) == 0;
  }
  Char escape_at(Location loc) const
  {
    if (opt_.e != '\0' && at(loc) == opt_.e)
      return at(loc + 1);
    if (at(loc) == '[' && at(loc + 1) == '[' && at(loc + 2) == ':' && at(loc + 4) == ':' && at(loc + 5) == ']' && at(loc + 6) == ']')
      return at(loc + 3);
    return '\0';
  }
  Char escapes_at(
      Location    loc,
      const char *escapes) const
  {
    if (opt_.e != '\0' && at(loc) == opt_.e && strchr(escapes, at(loc + 1)))
      return at(loc + 1);
    if (at(loc) == '[' && at(loc + 1) == '[' && at(loc + 2) == ':' && strchr(escapes, at(loc + 3)) && at(loc + 4) == ':' && at(loc + 5) == ']' && at(loc + 6) == ']')
      return at(loc + 3);
    return '\0';
  }
  static bool is_modified(
      int        mode,
      const Map& modifiers,
      Location   loc)
  {
    Map::const_iterator i = modifiers.find(mode);
    return (i != modifiers.end() && i->second.find(loc) != i->second.end());
  }
  static bool is_meta(Char c)
  {
    return c > 0x100;
  }
  static Opcode opcode_take(Index index)
  {
    return 0xff000000 | index;
  }
  static Opcode opcode_redo(void)
  {
    return 0xff000000 | IMAX;
  }
  static Opcode opcode_tail(Index index)
  {
    return 0xff7e0000 | index;
  }
  static Opcode opcode_head(Index index)
  {
    return 0xff7f0000 | index;
  }
  static Opcode opcode_goto(
      Char  lo,
      Char  hi,
      Index index)
  {
    if (!is_meta(lo)) return lo << 24 | hi << 16 | index;
    return 0xff000000 | (lo - META_MIN) << 16 | index;
  }
  static Opcode opcode_halt(void)
  {
    return 0x00ff0000 | IMAX;
  }
  static bool is_opcode_redo(Opcode opcode)
  {
    return opcode == (0xff000000 | IMAX);
  }
  static bool is_opcode_take(Opcode opcode)
  {
    return (opcode & 0xffff0000) == 0xff000000;
  }
  static bool is_opcode_tail(Opcode opcode)
  {
    return (opcode & 0xffff0000) == 0xff7e0000;
  }
  static bool is_opcode_head(Opcode opcode)
  {
    return (opcode & 0xffff0000) == 0xff7f0000;
  }
  static bool is_opcode_halt(Opcode opcode)
  {
    return opcode == (0x00ff0000 | IMAX);
  }
  static bool is_opcode_meta(Opcode opcode)
  {
    return (opcode & 0xff800000) == 0xff000000;
  }
  static bool is_opcode_meta(Opcode opcode, Char a)
  {
    return (opcode & 0xffff0000) == (0xff000000 | (a - META_MIN) << 16);
  }
  static bool is_opcode_match(
      Opcode          opcode,
      unsigned char c)
  {
    return c >= (opcode >> 24) && c <= (opcode >> 16 & 0xff);
  }
  static Char meta_of(Opcode opcode)
  {
    return META_MIN + (opcode >> 16 & 0xff);
  }
  static Char lo_of(Opcode opcode)
  {
    return is_opcode_meta(opcode) ? meta_of(opcode) : opcode >> 24;
  }
  static Char hi_of(Opcode opcode)
  {
    return is_opcode_meta(opcode) ? meta_of(opcode) : opcode >> 16 & 0xff;
  }
  static Index index_of(Opcode opcode)
  {
    return opcode & 0xffff;
  }
  Option                opt_; ///< pattern compiler options
  std::string           rex_; ///< regular expression string
  std::vector<Location> end_; ///< entries point to the subpattern's ending '|' or '\0'
  std::vector<bool>     acc_; ///< true if subpattern n is acceptable (state is reachable)
  size_t                vno_; ///< number of finite state machine vertices |V|
  size_t                eno_; ///< number of finite state machine edges |E|
  const Opcode         *opc_; ///< points to the generated opcode
  Index                 nop_; ///< number of opcodes generated
};

} // namespace reflex

#endif