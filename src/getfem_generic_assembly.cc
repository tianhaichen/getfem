/* -*- c++ -*- (enables emacs c++ mode) */
/*===========================================================================
 
 Copyright (C) 2013-2014 Yves Renard
 
 This file is a part of GETFEM++
 
 Getfem++  is  free software;  you  can  redistribute  it  and/or modify it
 under  the  terms  of the  GNU  Lesser General Public License as published
 by  the  Free Software Foundation;  either version 3 of the License,  or
 (at your option) any later version along with the GCC Runtime Library
 Exception either version 3.1 or (at your option) any later version.
 This program  is  distributed  in  the  hope  that it will be useful,  but
 WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 or  FITNESS  FOR  A PARTICULAR PURPOSE.  See the GNU Lesser General Public
 License and GCC Runtime Library Exception for more details.
 You  should  have received a copy of the GNU Lesser General Public License
 along  with  this program;  if not, write to the Free Software Foundation,
 Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301, USA.
 
===========================================================================*/

#include "getfem/getfem_config.h"
#include "gmm/gmm_blas.h"
#include <iomanip>
#include "getfem/getfem_omp.h"

extern "C" void daxpy_(const int *n, const double *alpha, const double *x,
		       const int *incx, double *y, const int *incy);


namespace getfem {

  //=========================================================================
  // Lexical analysis for the generic assembly langage
  //=========================================================================

  // Basic token types
  enum GA_TOKEN_TYPE {
    GA_INVALID = 0,// invalid token
    GA_END,        // string end
    GA_NAME,       // A variable or user defined nonlinear function name
    GA_SCALAR,     // A real number
    GA_PLUS,       // '+'
    GA_MINUS,      // '-'
    GA_UNARY_MINUS,// '-'
    GA_MULT,       // '*'
    GA_DIV,        // '/'
    GA_COLON,      // ':'
    GA_DOT,        // '.'
    GA_TMULT,      // '@' tensor product
    GA_COMMA,      // ','
    GA_DCOMMA,     // ',,'
    GA_SEMICOLON,  // ';'
    GA_DSEMICOLON, // ';;'
    GA_LPAR,       // '('
    GA_RPAR,       // ')'
    GA_LBRACKET,   // '['
    GA_RBRACKET,   // ']'
    GA_NB_TOKEN_TYPE
  };

  static GA_TOKEN_TYPE ga_char_type[256];
  static int ga_operator_priorities[GA_NB_TOKEN_TYPE];

  // Initialize ga_char_type and ga_operator_priorities arrays
  static bool init_ga_char_type(void) {
    for (int i = 0; i < 256; ++i) ga_char_type[i] = GA_INVALID;
    ga_char_type['+'] = GA_PLUS;        ga_char_type['-'] = GA_MINUS;
    ga_char_type['*'] = GA_MULT;        ga_char_type['/'] = GA_DIV;
    ga_char_type[':'] = GA_COLON;       ga_char_type['.'] = GA_DOT;
    ga_char_type['@'] = GA_TMULT;       ga_char_type[','] = GA_COMMA;
    ga_char_type[';'] = GA_SEMICOLON;   ga_char_type['('] = GA_LPAR;
    ga_char_type[')'] = GA_RPAR;        ga_char_type['['] = GA_LBRACKET;
    ga_char_type[']'] = GA_RBRACKET;    ga_char_type['_'] = GA_NAME;
    for (unsigned i = 'a'; i <= 'z'; ++i)  ga_char_type[i] = GA_NAME;
    for (unsigned i = 'A'; i <= 'Z'; ++i)  ga_char_type[i] = GA_NAME;
    for (unsigned i = '0'; i <= '9'; ++i)  ga_char_type[i] = GA_SCALAR;

    for (unsigned i = 0; i < GA_NB_TOKEN_TYPE; ++i)
      ga_operator_priorities[i] = 0;
    ga_operator_priorities[GA_PLUS] = 1;
    ga_operator_priorities[GA_MINUS] = 1;
    ga_operator_priorities[GA_MULT] = 2;
    ga_operator_priorities[GA_DIV] = 2;
    ga_operator_priorities[GA_COLON] = 2;
    ga_operator_priorities[GA_DOT] = 2;
    ga_operator_priorities[GA_TMULT] = 2;
    ga_operator_priorities[GA_UNARY_MINUS] = 3;

    return true;
  }

  static bool initialized = init_ga_char_type();

  // Get the next token in the string at position 'pos' end return its type
  static GA_TOKEN_TYPE ga_get_token(const std::string &expr,
                                    size_type &pos,
                                    size_type &token_pos,
                                    size_type &token_length) {
    bool fdot = false, fE = false;

    // Ignore white spaces
    while (expr[pos] == ' ' && pos < expr.size()) ++pos;
    token_pos = pos;
    token_length = 0;

    // Detecting end of expression
    if (pos >= expr.size()) return GA_END;

    // Treating the different cases (Operation, name or number)
    GA_TOKEN_TYPE type = ga_char_type[unsigned(expr[pos++])];
    ++token_length;
    switch (type) {
    case GA_DOT:
      if (pos >= expr.size() || ga_char_type[unsigned(expr[pos])] != GA_SCALAR)
        return type;
      fdot = true; type = GA_SCALAR;
    case GA_SCALAR:
      while (pos < expr.size()) {
        GA_TOKEN_TYPE ctype = ga_char_type[unsigned(expr[pos])];
        switch (ctype) {
        case GA_DOT:
          if (fdot) return type;
          fdot = true; ++pos; ++token_length; 
          break;
        case GA_NAME:
          if (fE || (expr[pos] != 'E' && expr[pos] != 'e')) return type;
          fE = true; fdot = true; ++pos; ++token_length;
          if (pos < expr.size()) {
            if (expr[pos] == '+' || expr[pos] == '-')
            { ++pos; ++token_length; }
          }
          if (pos >= expr.size()
              || ga_char_type[unsigned(expr[pos])] != GA_SCALAR)
            return GA_INVALID;
          break;
        case GA_SCALAR:
          ++pos; ++token_length; break;
        default:
          return type;
        }
      }
      return type;
    case GA_NAME:
      while (pos < expr.size()) {
        GA_TOKEN_TYPE ctype = ga_char_type[unsigned(expr[pos])];
        if (ctype != GA_SCALAR && ctype != GA_NAME) break;
        ++pos; ++token_length;
      }
      return type;
    case GA_COMMA:
      if (pos < expr.size() &&
          ga_char_type[unsigned(expr[pos])] == GA_COMMA) {
        ++pos; return GA_DCOMMA;
      }
      return type;
    case GA_SEMICOLON:
      if (pos < expr.size() &&
          ga_char_type[unsigned(expr[pos])] == GA_SEMICOLON) {
        ++pos; return GA_DSEMICOLON;
      }
      return type;
    default: return type;
    }
  }
  

  //=========================================================================
  // Tree structure for syntax analysis
  //=========================================================================

  enum GA_NODE_TYPE {
    GA_NODE_VOID = 0,
    GA_NODE_OP,
    GA_NODE_SCALAR,
    GA_NODE_VECTOR,
    GA_NODE_MATRIX,
    GA_NODE_TENSOR,
    GA_NODE_NAME,
    GA_NODE_PARAMS,
    GA_NODE_ALLINDICES,
    GA_NODE_C_MATRIX};

  struct ga_tree_node {
    GA_NODE_TYPE node_type;
    scalar_type val;
    base_vector vec;
    base_matrix mat;
    base_tensor t;
    size_type nbc1, nbc2, nbc3, pos;
    std::string name;
    GA_TOKEN_TYPE op_type;
    bool valid;
    ga_tree_node *parent; // only one for the moment ...
    std::vector<ga_tree_node *> children;

    ga_tree_node(void): node_type(GA_NODE_VOID), valid(true) {}
    ga_tree_node(GA_NODE_TYPE ty, size_type p)
      : node_type(ty), pos(p), valid(true) {}
    ga_tree_node(scalar_type v, size_type p)
      : node_type(GA_NODE_SCALAR), val(v), pos(p), valid(true) {}
    ga_tree_node(const char *n, size_type l, size_type p)
      : node_type(GA_NODE_NAME), pos(p), name(n, l), valid(true) {}
    ga_tree_node(GA_TOKEN_TYPE op, size_type p)
      : node_type(GA_NODE_OP), pos(p), op_type(op), valid(true) {}
    
  };

  typedef ga_tree_node *pga_tree_node;

  struct ga_tree {
    pga_tree_node root, current_node;

    void add_scalar(scalar_type val, size_type pos) {
      while (current_node && current_node->node_type != GA_NODE_OP)
        current_node = current_node->parent;
      if (current_node) {
        pga_tree_node new_node = new ga_tree_node(val, pos);
        current_node->children.push_back(new_node);
        new_node->parent = current_node;
        current_node = new_node;
      }
      else {
        GMM_ASSERT1(root == 0, "Invalid tree operation");
        current_node = root = new ga_tree_node(val, pos);
        root->parent = 0;
      }
    }

    void add_allindices(size_type pos) {
      while (current_node && current_node->node_type != GA_NODE_OP)
        current_node = current_node->parent;
      if (current_node) {
        pga_tree_node new_node = new ga_tree_node(GA_NODE_ALLINDICES, pos);
        current_node->children.push_back(new_node);
        new_node->parent = current_node;
        current_node = new_node;
      }
      else {
        GMM_ASSERT1(root == 0, "Invalid tree operation");
        current_node = root = new ga_tree_node(GA_NODE_ALLINDICES, pos);
        root->parent = 0;
      }
    }

    void add_name(const char *name, size_type length, size_type pos) {
      while (current_node && current_node->node_type != GA_NODE_OP)
        current_node = current_node->parent;
      if (current_node) {
        pga_tree_node new_node = new ga_tree_node(name, length, pos);
        current_node->children.push_back(new_node);
        new_node->parent = current_node;
        current_node = new_node;
      }
      else {
        GMM_ASSERT1(root == 0, "Invalid tree operation");
        current_node = root = new ga_tree_node(name, length, pos);
        root->parent = 0;
      }
    }

    void add_sub_tree(ga_tree &sub_tree) {
      if (current_node && (current_node->node_type == GA_NODE_PARAMS ||
                           current_node->node_type == GA_NODE_C_MATRIX)) {
        current_node->children.push_back(sub_tree.root);
        sub_tree.root->parent = current_node;
      } else {
        GMM_ASSERT1(sub_tree.root, "Invalid tree operation");
        while (current_node && current_node->node_type != GA_NODE_OP)
          current_node = current_node->parent;
        if (current_node) {
          current_node->children.push_back(sub_tree.root);
          sub_tree.root->parent = current_node;
          current_node = sub_tree.root;
        }
        else {
          GMM_ASSERT1(root == 0, "Invalid tree operation");
          current_node = root = sub_tree.root;
          root->parent = 0;
        }  
      }
      sub_tree.root = sub_tree.current_node = 0;
    }

    void add_params(size_type pos) {
      GMM_ASSERT1(current_node, "internal error");
      pga_tree_node new_node = new ga_tree_node(GA_NODE_PARAMS, pos);
      pga_tree_node parent =  current_node->parent;
      if (parent) {
        for (size_type i = 0; i < parent->children.size(); ++i)
          if (parent->children[i] == current_node)
            parent->children[i] = new_node;
      }
      else
        root = new_node;
      new_node->parent = current_node->parent;
      current_node->parent = new_node;
      new_node->children.push_back(current_node);
      current_node = new_node;
    }

    void add_matrix(size_type pos) {
      while (current_node && current_node->node_type != GA_NODE_OP)
        current_node = current_node->parent;
      if (current_node) {
        pga_tree_node new_node = new ga_tree_node(GA_NODE_C_MATRIX, pos);
        current_node->children.push_back(new_node);
        new_node->parent = current_node;
        current_node = new_node;
      }
      else {
        GMM_ASSERT1(root == 0, "Invalid tree operation");
        current_node = root = new ga_tree_node(GA_NODE_C_MATRIX, pos);
        root->parent = 0;
      }
      current_node->nbc1 = current_node->nbc2 = current_node->nbc3 = 0;
    }
    
    void add_op(GA_TOKEN_TYPE op_type, size_type pos) {
      while (current_node &&
             current_node->parent &&
             current_node->parent->node_type == GA_NODE_OP &&
             ga_operator_priorities[current_node->parent->op_type]
             >= ga_operator_priorities[op_type])
        current_node = current_node->parent;
      pga_tree_node new_node = new ga_tree_node(op_type, pos);
      if (current_node) {
        pga_tree_node parent = current_node->parent;
        if (parent) {
          new_node->parent = parent;
          for (size_type i = 0; i < parent->children.size(); ++i)
            if (parent->children[i] == current_node)
              parent->children[i] = new_node;
        } else {
          root = new_node; new_node->parent = 0;
        }
        new_node->children.push_back(current_node);
        current_node->parent = new_node;
      } else {
        if (root) new_node->children.push_back(root);
        root = new_node; new_node->parent = 0;
      }
      current_node = new_node;
    }

    void clear_node(pga_tree_node pnode) {
      if (pnode->valid) {
        pnode->valid = false;
        for (size_type i = 0; i < pnode->children.size(); ++i)
          clear_node(pnode->children[i]);
        delete pnode;
        current_node = 0;
      }
    }

    void clear(void)
    { if (root) clear_node(root); root = current_node = 0; }
    
    void clear_children(pga_tree_node pnode) {
      for (size_type i = 0; i < pnode->children.size(); ++i)
        clear_node(pnode->children[i]);
      pnode->children.resize(0);
    }


    ga_tree(void) : root(0), current_node(0) {}
    ~ga_tree() { clear(); }
  };

  static void verify_tree(pga_tree_node pnode, pga_tree_node parent) {
    GMM_ASSERT1(pnode->parent == parent,
                "Invalid tree node " << pnode->node_type);
    for (size_type i = 1; i < pnode->children.size(); ++i)
      verify_tree(pnode->children[i], pnode);
  }

  static void ga_print_node(pga_tree_node pnode) {
    
    switch(pnode->node_type) {
    case GA_NODE_OP:
      cout << "(";
      switch (pnode->op_type) {
      case GA_PLUS: cout << "+"; break;
      case GA_MINUS: case GA_UNARY_MINUS: cout << "-"; break;
      case GA_MULT: cout << "*"; break;
      case GA_DIV: cout << "/"; break;
      case GA_COLON: cout << ":"; break;
      case GA_DOT: cout << "."; break;
      case GA_TMULT: cout << "@"; break;
      default: cout << "Invalid or not taken into account operation"; break;
      }
      for (size_type i = 0; i < pnode->children.size(); ++i)
        { cout << " "; ga_print_node(pnode->children[i]); }
      cout << ")";
      break;

    case GA_NODE_SCALAR:
      cout << pnode->val;
      GMM_ASSERT1(pnode->children.size() == 0, "Invalid tree");
      break;

    case GA_NODE_VECTOR:
      cout << "[[";
      for (size_type i = 0; i < gmm::vect_size(pnode->vec); ++i)
        { if (i != 0) cout << "; "; cout << pnode->vec[i]; }
      cout << "]]";
      GMM_ASSERT1(pnode->children.size() == 0, "Invalid tree");
      break;

    case GA_NODE_MATRIX:
      cout << "[[";
      for (size_type i = 0; i < gmm::mat_nrows(pnode->mat); ++i) {
        if (i != 0) cout << "; ";
        for (size_type j = 0; j < gmm::mat_ncols(pnode->mat); ++j)
          { if (j != 0) cout << ", "; cout << pnode->mat(i,j); }
      }
      cout << "]]";
      GMM_ASSERT1(pnode->children.size() == 0, "Invalid tree");
      break;

    case GA_NODE_TENSOR:
      cout << "[[";
      for (size_type i = 0; i < pnode->t.size(0); ++i) {
        if (i != 0) cout << ";; ";
        for (size_type j = 0; j < pnode->t.size(1); ++j) {
          if (j != 0) cout << ",, "; 
          for (size_type k = 0; k < pnode->t.size(2); ++k) {
            if (k != 0) cout << "; "; 
            for (size_type l = 0; l < pnode->t.size(3); ++l)
              { if (l != 0) cout << ", "; cout << pnode->t(i,j,k,l); }
          }
        }
      }
      cout << "]]";
      GMM_ASSERT1(pnode->children.size() == 0, "Invalid tree");
      break;

    case GA_NODE_ALLINDICES:
      cout << ":";
      GMM_ASSERT1(pnode->children.size() == 0, "Invalid tree");
      break;

    case GA_NODE_PARAMS:
      GMM_ASSERT1(pnode->children.size(), "Invalid tree");
      ga_print_node(pnode->children[0]);
      cout << "(";
      for (size_type i = 1; i < pnode->children.size(); ++i)
        { if (i > 1) cout << ", "; ga_print_node(pnode->children[i]); }
      cout << ")";
      break;

    case GA_NODE_NAME:
      cout << pnode->name;
      GMM_ASSERT1(pnode->children.size() == 0, "Invalid tree");
      break;

    case GA_NODE_C_MATRIX:
      GMM_ASSERT1(pnode->children.size(), "Invalid tree");
      cout << "[";
      for (size_type i = 0; i < pnode->children.size(); ++i) {
        if (i > 0) {
          if (i%pnode->nbc1 != 0) cout << ", ";
          else {
            if (pnode->nbc2 > 1 || pnode->nbc3 > 1) {
              if (i%(pnode->nbc1*pnode->nbc2) != 0) cout << "; ";
              else if (i%(pnode->nbc1*pnode->nbc2*pnode->nbc3) != 0)
                cout << ",, ";
              else cout << ";; ";
            } else cout << "; ";
          }
        }
        ga_print_node(pnode->children[i]);
      }
      cout << "]";
      break;

    default: cout << "Invalid or not taken into account node type"; break;
    }
  }
 

  static void ga_print_tree(const ga_tree &tree) {
    if (tree.root) verify_tree(tree.root, 0);
    if (tree.root) ga_print_node(tree.root); else cout << "Empty tree";
    cout << endl;
  }

  //=========================================================================
  // Syntax analysis for the generic assembly langage
  //=========================================================================
  
  
  static void ga_syntax_error(const std::string &expr, size_type pos,
                              const std::string &msg) {
    int first = std::max(0, int(pos)-40);
    int last = std::min(int(pos)+20, int(expr.size()));
    if (last - first < 60)
      first = std::max(0, int(pos)-40-(60-last+first));
    if (last - first < 60)
      last = std::min(int(pos)+20+(60-last+first),int(expr.size()));
    
    if (first > 0) cerr << "...";
    cerr << expr.substr(first, last-first);
    if (last < int(expr.size())) cerr << "...";
    cerr << endl;
    if (first > 0) cerr << "   ";
    if (int(pos) > first) cerr << std::setw(int(pos)-first) << ' ';
    cerr << '|' << endl << msg << endl;
    GMM_ASSERT1(false, "Error in assembly string" );
  }
             
  // Read a term with a pushdown automaton.
  static GA_TOKEN_TYPE ga_read_term(const std::string &expr,
                                    size_type &pos, ga_tree &tree) {
    size_type token_pos, token_length;
    GA_TOKEN_TYPE t_type;
    int state = 1; // 1 = reading term, 2 = reading after term 

    for (;;) {
      
      t_type = ga_get_token(expr, pos, token_pos, token_length);

      switch (state) {

      case 1:
        switch (t_type) {
        case GA_SCALAR:
          { 
            char *endptr; const char *nptr = &(expr[token_pos]);
            scalar_type s_read = ::strtod(nptr, &endptr);
            if (endptr == nptr)
              ga_syntax_error(expr, token_pos, "Bad numeric format.");
            tree.add_scalar(s_read, token_pos);
          }
          state = 2; break;

        case GA_COLON:
          tree.add_allindices(token_pos);
          state = 2; break;
          
        case GA_NAME:
          tree.add_name(&(expr[token_pos]), token_length, token_pos);
          state = 2; break;

        case GA_MINUS: // unary -
          tree.add_op(GA_UNARY_MINUS, token_pos);
        case GA_PLUS:  // unary +
          state = 1; break;

        case GA_LPAR: // Parenthesed expression
          {
            ga_tree sub_tree;
            GA_TOKEN_TYPE r_type;
            r_type = ga_read_term(expr, pos, sub_tree);
            if (r_type != GA_RPAR)
              ga_syntax_error(expr, pos-1, "Unbalanced parenthesis.");
            tree.add_sub_tree(sub_tree);
            state = 2;
          }
          break;

        case GA_LBRACKET: // Constant vector/matrix or tensor
          {
            ga_tree sub_tree;
            GA_TOKEN_TYPE r_type;
            size_type nbc1 = 0, nbc2 = 0, nbc3 = 0, n1 = 0, n2 = 0, n3 = 0;
            bool foundsemi = false, founddcomma = false, founddsemi = false;
            tree.add_matrix(token_pos);
            for(;;) {
              r_type = ga_read_term(expr, pos, sub_tree);
              ++n1; ++n2; ++n3;
              if (!foundsemi) ++nbc1;
              if (!founddcomma) ++nbc2;
              if (!founddsemi) ++nbc3;

              switch(r_type) {
              case GA_COMMA: break;
              case GA_SEMICOLON: foundsemi = true; n1 = 0; break;
              case GA_DCOMMA: founddcomma = true; n2 = 0; n1 = 0; break;
              case GA_DSEMICOLON:
                founddsemi = true; n3 = 0; n2 = 0; n1 = 0; break;
              case GA_RBRACKET:
                if (n1 != nbc1 || n2 != nbc2 || n3 != nbc3 ||
                    (founddcomma && !founddsemi) ||
                    (!founddcomma && founddsemi))
                  ga_syntax_error(expr, pos-1, "Bad constant "
                                  "vector/matrix/tensor format. ");
                tree.current_node->nbc1 = nbc1;
                if (founddcomma) {
                  tree.current_node->nbc2 = nbc2/nbc1;
                  tree.current_node->nbc3 = nbc3/nbc2;
                } else {
                  tree.current_node->nbc2 = tree.current_node->nbc3 = 1;
                }
                break;
              default:
                ga_syntax_error(expr, pos-1, "The constant "
                                "vector/matrix/tensor components should be "
                                "separated by ',', ';', ',,' and ';;' and "
                                "be ended by ']'.");
                break;
              }
         
              tree.add_sub_tree(sub_tree);
              if (r_type == GA_RBRACKET) break;
            }
            state = 2;
          }
          break;

        default: ga_syntax_error(expr, token_pos, "Unexpected token.");
        }
        break;

      case 2:
        switch (t_type) {
        case GA_PLUS: case GA_MINUS: case GA_MULT: case GA_DIV:
        case GA_COLON: case GA_DOT: case GA_TMULT:
          tree.add_op(t_type, token_pos);
          state = 1; break;
        case GA_END: case GA_RPAR: case GA_COMMA: case GA_DCOMMA:
        case GA_RBRACKET: case GA_SEMICOLON: case GA_DSEMICOLON:
          return t_type;
        case GA_LPAR: // Parameter list
          {
            ga_tree sub_tree;
            GA_TOKEN_TYPE r_type;
            tree.add_params(token_pos);
            for(;;) {
              r_type = ga_read_term(expr, pos, sub_tree);
              if (r_type != GA_RPAR && r_type != GA_COMMA)
                ga_syntax_error(expr, pos-((r_type != GA_END)?1:0),
                               "Parameters should be separated "
                               "by ',' and parameter list ended by ')'.");
              tree.add_sub_tree(sub_tree);
              if (r_type == GA_RPAR) break;
            }
            state = 2;
          }
          break;
          
        default: ga_syntax_error(expr, token_pos, "Unexpected token.");
        }
        break;
      }
    }

    return GA_INVALID;
  }

  // Syntax analysis of a string. Conversion to a tree.
  void ga_read_string(const std::string &expr, ga_tree &tree) {
    size_type pos = 0;
    tree.clear();
    GA_TOKEN_TYPE t = ga_read_term(expr, pos, tree);
    switch (t) {
    case GA_RPAR: ga_syntax_error(expr, pos-1, "Unbalanced parenthesis.");
    case GA_RBRACKET: ga_syntax_error(expr, pos-1, "Unbalanced braket.");
    case GA_END: break;
    default: ga_syntax_error(expr, pos-1, "Unexpected token.");
    }
  }         

  //=========================================================================
  // Semantic analysis, simplification  (and compilation ?).
  //=========================================================================

  static void ga_node_analysis(const std::string &expr, ga_tree &tree,
                               pga_tree_node pnode) {

    bool all_sc = true, all_sc_but_first = true;
    for (size_type i = 0; i < pnode->children.size(); ++i) {
      ga_node_analysis(expr, tree, pnode->children[i]);
      all_sc = all_sc && (pnode->children[i]->node_type == GA_NODE_SCALAR);
      if (i > 0)
        all_sc_but_first = all_sc_but_first
          && (pnode->children[i]->node_type == GA_NODE_SCALAR);
    }
    
    switch (pnode->node_type) {
    case GA_NODE_SCALAR: case GA_NODE_ALLINDICES: break;
    case GA_NODE_OP:
      switch(pnode->op_type) {
      case GA_PLUS: case GA_MINUS: // + traiter les cas qui ne marceh pas
        if (all_sc) {
          pnode->node_type = GA_NODE_SCALAR;
          pnode->val = pnode->children[0]->val + pnode->children[1]->val *
            ((pnode->op_type == GA_MINUS) ? scalar_type(-1) : scalar_type(1));
          tree.clear_children(pnode);
        } else if (pnode->children[0]->node_type == GA_NODE_VECTOR &&
                   pnode->children[1]->node_type == GA_NODE_VECTOR) {
          pnode->node_type = GA_NODE_VECTOR;
          pnode->vec = pnode->children[0]->vec;
          if (gmm::vect_size(pnode->vec)
              != gmm::vect_size(pnode->children[1]->vec))
            ga_syntax_error(expr, pnode->pos, "Addition or substraction "
                            "of vectors of different sizes");
          if (pnode->op_type == GA_MINUS)
            gmm::add(pnode->children[1]->vec, pnode->vec);
          else
            gmm::add(gmm::scaled(pnode->children[1]->vec, scalar_type(-1)),
                     pnode->vec);
          tree.clear_children(pnode);
        } else if (pnode->children[0]->node_type == GA_NODE_MATRIX &&
                   pnode->children[1]->node_type == GA_NODE_MATRIX) {
          pnode->node_type = GA_NODE_MATRIX;
          pnode->mat = pnode->children[0]->mat;
          if (gmm::mat_nrows(pnode->mat)
              != gmm::mat_nrows(pnode->children[1]->mat) ||
              gmm::mat_ncols(pnode->mat)
              != gmm::mat_ncols(pnode->children[1]->mat))
            ga_syntax_error(expr, pnode->pos, "Addition or substraction "
                            "of matrices of different sizes");
          if (pnode->op_type == GA_MINUS)
            gmm::add(gmm::scaled(pnode->children[1]->mat, scalar_type(-1)),
                     pnode->mat);
          else
            gmm::add(pnode->children[1]->mat, pnode->mat);
          tree.clear_children(pnode);
        } else if (pnode->children[0]->node_type == GA_NODE_TENSOR &&
                   pnode->children[1]->node_type == GA_NODE_TENSOR) {
          pnode->node_type = GA_NODE_TENSOR;
          pnode->t = pnode->children[0]->t;
          if (pnode->t.size(0) != pnode->children[1]->t.size(0) ||
              pnode->t.size(1) != pnode->children[1]->t.size(1) ||
              pnode->t.size(2) != pnode->children[1]->t.size(2) ||
              pnode->t.size(3) != pnode->children[1]->t.size(3))
            ga_syntax_error(expr, pnode->pos, "Addition or substraction "
                            "of tensors of different sizes");
          if (pnode->op_type == GA_MINUS)
            pnode->t -= pnode->children[1]->t;
          else
            pnode->t += pnode->children[1]->t;
          tree.clear_children(pnode);
        } 
        break;

      case GA_DOT:
        if (pnode->children[0]->node_type == GA_NODE_VECTOR &&
            pnode->children[1]->node_type == GA_NODE_VECTOR) {
          pnode->node_type = GA_NODE_SCALAR;
          if (gmm::vect_size(pnode->children[0]->vec)
              != gmm::vect_size(pnode->children[1]->vec))
            ga_syntax_error(expr, pnode->pos, "Incompatible sizes in "
                            "scalar product.");
          pnode->val = gmm::vect_sp(pnode->children[0]->vec,
                                    pnode->children[1]->vec);
          tree.clear_children(pnode);
        }
        break;

      case GA_COLON:
        if (pnode->children[0]->node_type == GA_NODE_MATRIX &&
            pnode->children[1]->node_type == GA_NODE_MATRIX) {
          pnode->node_type = GA_NODE_SCALAR;
          if (gmm::mat_nrows(pnode->children[0]->mat)
              != gmm::mat_nrows(pnode->children[1]->mat) ||
              gmm::mat_ncols(pnode->children[0]->mat)
              != gmm::mat_ncols(pnode->children[1]->mat))
            ga_syntax_error(expr, pnode->pos, "Incompatible sizes in "
                            "Frobenius product.");
          pnode->val = gmm::vect_sp(pnode->children[0]->mat.as_vector(),
                                    pnode->children[1]->mat.as_vector());
          tree.clear_children(pnode);
        }
        break;

      case GA_TMULT:
        if (all_sc) {
          pnode->node_type = GA_NODE_SCALAR;
          pnode->val = pnode->children[0]->val * pnode->children[1]->val;
          tree.clear_children(pnode);
        } else if (pnode->children[0]->node_type == GA_NODE_VECTOR &&
                   pnode->children[1]->node_type == GA_NODE_VECTOR) {
          pnode->node_type = GA_NODE_MATRIX;
          size_type nbl = gmm::vect_size(pnode->children[0]->vec);
          size_type nbc = gmm::vect_size(pnode->children[1]->vec);
          gmm::resize(pnode->mat, nbl, nbc);
          for (size_type i = 0; i < nbl; ++i)
            for (size_type j = 0; j < nbc; ++j)
              pnode->mat(i,j) = pnode->children[0]->vec[i]
                * pnode->children[1]->vec[j];
          tree.clear_children(pnode);
        } else if (pnode->children[0]->node_type == GA_NODE_MATRIX &&
                   pnode->children[1]->node_type == GA_NODE_MATRIX) {
          pnode->node_type = GA_NODE_TENSOR;
          bgeot::multi_index mi(gmm::mat_nrows(pnode->children[0]->mat),
                                gmm::mat_ncols(pnode->children[0]->mat),
                                gmm::mat_nrows(pnode->children[1]->mat),
                                gmm::mat_ncols(pnode->children[1]->mat));
          pnode->t.adjust_sizes(mi);
          for (size_type i = 0; i < mi[0]; ++i)
            for (size_type j = 0; j < mi[1]; ++j)
              for (size_type k = 0; k < mi[2]; ++k)
                for (size_type l = 0; l < mi[3]; ++l)
                  pnode->t(i,j,k,l) = pnode->children[0]->mat(i,j)
                    * pnode->children[1]->mat(k,l);
          tree.clear_children(pnode);
        }

      case GA_MULT:
        if (all_sc) {
          pnode->node_type = GA_NODE_SCALAR;
          pnode->val = pnode->children[0]->val * pnode->children[1]->val;
          tree.clear_children(pnode);
        } else if (pnode->children[0]->node_type == GA_NODE_VECTOR &&
                   pnode->children[1]->node_type == GA_NODE_SCALAR) {
          pnode->node_type = GA_NODE_VECTOR;
          pnode->vec = pnode->children[0]->vec;
          gmm::scale(pnode->vec, pnode->children[1]->val);
          tree.clear_children(pnode);
        } else if (pnode->children[0]->node_type == GA_NODE_MATRIX &&
                   pnode->children[1]->node_type == GA_NODE_SCALAR) {
          pnode->node_type = GA_NODE_MATRIX;
          pnode->mat = pnode->children[0]->mat;
          gmm::scale(pnode->mat, pnode->children[1]->val);
          tree.clear_children(pnode);
        } else if (pnode->children[0]->node_type == GA_NODE_SCALAR &&
                   pnode->children[1]->node_type == GA_NODE_VECTOR) {
          pnode->node_type = GA_NODE_VECTOR;
          pnode->vec = pnode->children[1]->vec;
          gmm::scale(pnode->vec, pnode->children[0]->val);
          tree.clear_children(pnode);
        } else if (pnode->children[0]->node_type == GA_NODE_SCALAR &&
                   pnode->children[1]->node_type == GA_NODE_MATRIX) {
          pnode->node_type = GA_NODE_MATRIX;
          pnode->mat = pnode->children[1]->mat;
          gmm::scale(pnode->mat, pnode->children[0]->val);
          tree.clear_children(pnode);
        } else if (pnode->children[0]->node_type == GA_NODE_SCALAR &&
                   pnode->children[1]->node_type == GA_NODE_TENSOR) {
          pnode->node_type = GA_NODE_TENSOR;
          pnode->t = pnode->children[1]->t;
          gmm::scale(pnode->t.as_vector(), pnode->children[0]->val);
          tree.clear_children(pnode);
        } else if (pnode->children[0]->node_type == GA_NODE_TENSOR &&
                   pnode->children[1]->node_type == GA_NODE_SCALAR) {
          pnode->node_type = GA_NODE_TENSOR;
          pnode->t = pnode->children[0]->t;
          gmm::scale(pnode->t.as_vector(), pnode->children[1]->val);
          tree.clear_children(pnode);
        } else if (pnode->children[0]->node_type == GA_NODE_MATRIX &&
                   pnode->children[1]->node_type == GA_NODE_VECTOR) {
          pnode->node_type = GA_NODE_VECTOR;
          gmm::resize(pnode->vec, gmm::mat_nrows(pnode->children[0]->mat));
          if (gmm::mat_ncols(pnode->children[0]->mat)
              != gmm::vect_size(pnode->children[1]->vec))
            ga_syntax_error(expr, pnode->pos, "Incompatible sizes in "
                            "matrix-vector multiplication.");
          gmm::mult(pnode->children[0]->mat, pnode->children[1]->vec,
                    pnode->vec);
          tree.clear_children(pnode);
        } else if (pnode->children[0]->node_type == GA_NODE_MATRIX &&
                   pnode->children[1]->node_type == GA_NODE_MATRIX) {
          pnode->node_type = GA_NODE_MATRIX;
          gmm::resize(pnode->mat, gmm::mat_nrows(pnode->children[0]->mat),
                      gmm::mat_ncols(pnode->children[1]->mat));
          if (gmm::mat_ncols(pnode->children[0]->mat)
              != gmm::mat_nrows(pnode->children[1]->mat))
            ga_syntax_error(expr, pnode->pos, "Incompatible sizes in "
                            "matrix-matrix multiplication.");
          gmm::mult(pnode->children[0]->mat, pnode->children[1]->mat,
                    pnode->mat);
          tree.clear_children(pnode);
        } else if (pnode->children[0]->node_type == GA_NODE_TENSOR &&
                   pnode->children[1]->node_type == GA_NODE_MATRIX) {
          pnode->node_type = GA_NODE_MATRIX;
          gmm::resize(pnode->mat, pnode->children[0]->t.size(0),
                      pnode->children[0]->t.size(1));
          if (pnode->children[0]->t.size(2)
              != gmm::mat_nrows(pnode->children[1]->mat) &&
              pnode->children[0]->t.size(3)
              != gmm::mat_ncols(pnode->children[1]->mat))
            ga_syntax_error(expr, pnode->pos, "Incompatible sizes in "
                            "tensor-matrix multiplication.");
          pnode->children[0]->t.mat_mult(pnode->children[1]->mat, pnode->mat);
          tree.clear_children(pnode);
        }
        break;

      case GA_DIV: // should control that the second argument is scalar valued
        if (pnode->children[1]->node_type == GA_NODE_SCALAR &&
            pnode->children[1]->val == scalar_type(0))
          ga_syntax_error(expr, pnode->children[1]->pos, "Division by zero");
        if (all_sc) {
          pnode->node_type = GA_NODE_SCALAR;
          pnode->val = pnode->children[0]->val / pnode->children[1]->val;
          tree.clear_children(pnode);
        } else if (pnode->children[0]->node_type == GA_NODE_VECTOR &&
                   pnode->children[1]->node_type == GA_NODE_SCALAR) {
          pnode->node_type = GA_NODE_VECTOR;
          pnode->vec = pnode->children[0]->vec;
          gmm::scale(pnode->vec, scalar_type(1)/pnode->children[1]->val);
          tree.clear_children(pnode);
        } else if (pnode->children[0]->node_type == GA_NODE_MATRIX &&
                   pnode->children[1]->node_type == GA_NODE_SCALAR) {
          pnode->node_type = GA_NODE_MATRIX;
          pnode->mat = pnode->children[0]->mat;
          gmm::scale(pnode->mat, scalar_type(1)/pnode->children[1]->val);
          tree.clear_children(pnode);
        } else if (pnode->children[0]->node_type == GA_NODE_TENSOR &&
                   pnode->children[1]->node_type == GA_NODE_SCALAR) {
          pnode->node_type = GA_NODE_TENSOR;
          pnode->t = pnode->children[0]->t;
          gmm::scale(pnode->t.as_vector(),
                     scalar_type(1)/pnode->children[1]->val);
          tree.clear_children(pnode);
        }
        break;
        

      case GA_UNARY_MINUS:
        if (all_sc) {
          pnode->node_type = GA_NODE_SCALAR;
          pnode->val = -(pnode->children[0]->val);
          tree.clear_children(pnode);
        } else if (pnode->children[0]->node_type == GA_NODE_VECTOR) {
          pnode->node_type = GA_NODE_VECTOR;
          pnode->vec = pnode->children[0]->vec;
          gmm::scale(pnode->vec, scalar_type(-1));
          tree.clear_children(pnode);
        } else if (pnode->children[0]->node_type == GA_NODE_MATRIX) {
          pnode->node_type = GA_NODE_MATRIX;
          pnode->mat = pnode->children[0]->mat;
          gmm::scale(pnode->mat, scalar_type(-1));
          tree.clear_children(pnode);
        } else if (pnode->children[0]->node_type == GA_NODE_TENSOR) {
          pnode->node_type = GA_NODE_TENSOR;
          pnode->t = pnode->children[0]->t;
          gmm::scale(pnode->t.as_vector(), scalar_type(-1));
          tree.clear_children(pnode);
        }
        break;


      default:GMM_ASSERT1(false, "Unexpected operation. Internal error.");
      }
      break;

    case GA_NODE_C_MATRIX: // Should control that all is scalar valued
      if (all_sc) {
        size_type nbc1 = pnode->nbc1, nbc2 = pnode->nbc2, nbc3 = pnode->nbc3;
        size_type nbl = pnode->children.size() / (nbc1*nbc2*nbc3);
        if (nbc1 == 1 && nbc2 == 1 && nbc3 == 1 && nbl == 1) {
          pnode->node_type = GA_NODE_SCALAR;
          pnode->val = pnode->children[0]->val;
        } else if (nbc1 == 1 && nbc2 == 1 && nbc3 == 1) {
          pnode->node_type = GA_NODE_VECTOR;
          gmm::resize(pnode->vec, nbl);
          for (size_type i = 0; i < nbl; ++i)
            pnode->vec[i] = pnode->children[i]->val;
        } else if (nbc2 == 1 && nbc3 == 1) {
          pnode->node_type = GA_NODE_MATRIX;
          gmm::resize(pnode->mat, nbl, nbc1);
          for (size_type i = 0; i < nbl; ++i)
            for (size_type j = 0; j < nbc1; ++j)
              pnode->mat(i,j) = pnode->children[i*nbc1+j]->val;
        } else {
          pnode->node_type = GA_NODE_TENSOR;
          bgeot::multi_index mi(nbl, nbc3, nbc2, nbc1);
          pnode->t.adjust_sizes(mi);
          size_type n = 0;
          for (size_type i = 0; i < nbl; ++i)
            for (size_type j = 0; j < nbc3; ++j)
              for (size_type k = 0; k < nbc2; ++k)
                for (size_type l = 0; l < nbc1; ++l)
                  pnode->t(i,j,k,l) = pnode->children[n++]->val;
        }
        tree.clear_children(pnode);
      }
      break;
      
    case GA_NODE_PARAMS:
      switch(pnode->children[0]->node_type) {
      case GA_NODE_SCALAR:
        if (pnode->children.size() != 2 ||
            (all_sc_but_first && size_type(pnode->children[1]->val) != 1)
            || (!all_sc_but_first &&
                pnode->children[1]->node_type != GA_NODE_ALLINDICES))
          ga_syntax_error(expr, pnode->pos, "Bad index format for a scalar");
        pnode->node_type = GA_NODE_SCALAR;
        pnode->val = pnode->children[0]->val;
        tree.clear_children(pnode);
        break;
      case GA_NODE_VECTOR:
        if (pnode->children.size() != 2)
            ga_syntax_error(expr, pnode->pos,
                            "Bad index format for a vector");
        if (all_sc_but_first) {
          pnode->node_type = GA_NODE_SCALAR;
          size_type i = size_type(::round(pnode->children[1]->val)-1);
          if (i >= gmm::vect_size(pnode->children[0]->vec))
            ga_syntax_error(expr, pnode->pos, "Index out of range");
          pnode->val = pnode->children[0]->vec[i];
          
        } else if (pnode->children[1]->node_type == GA_NODE_ALLINDICES) {
          pnode->node_type = GA_NODE_VECTOR;
          pnode->vec = pnode->children[0]->vec;
        } else ga_syntax_error(expr, pnode->pos, "Not constant index values");
        tree.clear_children(pnode);
        break;
      case GA_NODE_MATRIX:
        if (pnode->children.size() != 3)
          ga_syntax_error(expr, pnode->pos,
                          "Bad index format for a matrix");
        if (all_sc_but_first) {
          pnode->node_type = GA_NODE_SCALAR;
          size_type i = size_type(::round(pnode->children[1]->val)-1);
          size_type j = size_type(::round(pnode->children[2]->val)-1);
          if (i >= gmm::mat_nrows(pnode->children[0]->mat) ||
              j >= gmm::mat_ncols(pnode->children[0]->mat))
            ga_syntax_error(expr, pnode->pos, "Index out of range");
          pnode->val = pnode->children[0]->mat(i,j);
        } else if (pnode->children[1]->node_type == GA_NODE_ALLINDICES &&
                   pnode->children[2]->node_type == GA_NODE_ALLINDICES) {
          pnode->node_type = GA_NODE_MATRIX;
          pnode->mat = pnode->children[0]->mat;
        } else if (pnode->children[1]->node_type == GA_NODE_ALLINDICES &&
                   pnode->children[2]->node_type == GA_NODE_SCALAR) {
          size_type j = size_type(::round(pnode->children[2]->val)-1);
          if (j >= gmm::mat_ncols(pnode->children[0]->mat))
            ga_syntax_error(expr, pnode->pos, "Index out of range");
          pnode->node_type = GA_NODE_VECTOR;
          gmm::resize(pnode->vec, gmm::mat_nrows(pnode->children[0]->mat));
          gmm::copy(gmm::mat_col(pnode->children[0]->mat, j), pnode->vec);
        } else if (pnode->children[1]->node_type == GA_NODE_SCALAR &&
                   pnode->children[2]->node_type == GA_NODE_ALLINDICES) {
          size_type i = size_type(::round(pnode->children[1]->val)-1);
          if (i >= gmm::mat_nrows(pnode->children[0]->mat))
            ga_syntax_error(expr, pnode->pos, "Index out of range");
          pnode->node_type = GA_NODE_VECTOR;
          gmm::resize(pnode->vec, gmm::mat_ncols(pnode->children[0]->mat));
          gmm::copy(gmm::mat_row(pnode->children[0]->mat, i), pnode->vec);
        } else ga_syntax_error(expr, pnode->pos, "Not constant index values");
        tree.clear_children(pnode);
        break;
      case GA_NODE_TENSOR:
        {
          if (pnode->children.size() != 5)
            ga_syntax_error(expr, pnode->pos,
                          "Bad index format for a tensor");
          bool ai1 = (pnode->children[1]->node_type == GA_NODE_ALLINDICES);
          bool ai2 = (pnode->children[2]->node_type == GA_NODE_ALLINDICES);
          bool ai3 = (pnode->children[3]->node_type == GA_NODE_ALLINDICES);
          bool ai4 = (pnode->children[4]->node_type == GA_NODE_ALLINDICES);
          size_type nai = (ai1 ? 1:0)+(ai2 ? 1:0)+(ai3 ? 1:0)+(ai4 ? 1:0);
          if ((!ai1 && pnode->children[1]->node_type != GA_NODE_SCALAR) ||
              (!ai2 && pnode->children[2]->node_type != GA_NODE_SCALAR) ||
              (!ai3 && pnode->children[3]->node_type != GA_NODE_SCALAR) ||
              (!ai4 && pnode->children[4]->node_type != GA_NODE_SCALAR))
            ga_syntax_error(expr, pnode->pos, "Not constant index values");
          size_type i = size_type(::round(pnode->children[1]->val)-1);
          size_type j = size_type(::round(pnode->children[2]->val)-1);
          size_type k = size_type(::round(pnode->children[3]->val)-1);
          size_type l = size_type(::round(pnode->children[4]->val)-1);
          if ((!ai1 && i >= pnode->children[0]->t.size(0)) ||
              (!ai2 && j >= pnode->children[0]->t.size(1)) ||
              (!ai3 && k >= pnode->children[0]->t.size(2)) ||
              (!ai4 && l >= pnode->children[0]->t.size(3)))
            ga_syntax_error(expr, pnode->pos, "Index out of range");

          switch (nai) {
          case 0:
            pnode->node_type = GA_NODE_SCALAR;
            pnode->val = pnode->children[0]->t(i,j,k,l);
            break;
          case 1:
            {
              pnode->node_type = GA_NODE_VECTOR;
              size_type i1 = (ai1 ? 0:0)+(ai2 ? 1:0)+(ai3 ? 2:0)+(ai4 ? 3:0);
              cout << "i1 = " << i1 << endl;
              cout << "size(short_type)=" << sizeof(short_type) << endl;
              bgeot::multi_index mi(i,j,k,l);
              gmm::resize(pnode->vec, pnode->children[0]->t.size(i1));
              for (size_type n = 0; n < gmm::vect_size(pnode->vec); ++n) {
                mi[i1] = n;
                cout << "mi = " << mi << endl;
                pnode->vec[i] = pnode->children[0]->t(mi);
              }
            }
            break;
          case 2:
            {
              size_type i1 = 0, i2 = 1;
              if (ai1) { ++i1; ++i2; }
              if (ai2) { if (i1 == 1) ++i1; ++i2; }
              if (ai3) { if (i2 == 2) ++i2; }
              // ...
              GMM_ASSERT1(false, "To be done");
            }
            break;
          case 3: ga_syntax_error(expr, pnode->pos, "Reduction to an order 3 "
                                  "tensor is not allowed.");
          case 4:
            pnode->node_type = GA_NODE_TENSOR;
            pnode->t = pnode->children[0]->t;
            break;
          }
          tree.clear_children(pnode);
        }
        break;
      default: ga_syntax_error(expr, pnode->pos,
                               "Index on unexpected node type.");
      }
      break;

      
    default:GMM_ASSERT1(false, "Unexpected node type. Internal error.");
    }
    
  }

  //=========================================================================
  // Small test for debug
  //=========================================================================

  
  static std::string expr="([1,2;3,4]@[1,2;1,2])(:,2,1,1)(1)+ [1,2;3,4](1,:)(2)";
  // static std::string expr="[1,2;3,4]@[1,2;1,2]*[2,3;2,1]";
  // static std::string expr="[1,1;1,2,,1,1;1,2;;1,1;1,2,,1,1;1,3]";
  // static std::string expr = "p*Trace(Grad_Test_u) + Test_u(1,2)+1.0E-1";
  // static std::string expr = "-(5+(2*3)+2)/3";


  void lex_analysis(void) {
    ga_tree tree;
    ga_read_string(expr, tree);
    ga_print_tree(tree);
    ga_node_analysis(expr, tree, tree.root);
    ga_print_tree(tree);
  }
  

} /* end of namespace */
