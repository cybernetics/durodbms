/* $Id$ */

%{
#define YYDEBUG 1
#include <rel/rdb.h>
#include <gen/strfns.h>

extern RDB_transaction *expr_txp;
extern RDB_expression *resultp;

static RDB_table *
expr_to_table (const RDB_expression *exp);

enum {
    DURO_MAX_LLEN = 200
};

%}

%union {
    RDB_expression *exp;
    struct {
        int attrc;
        char *attrv[DURO_MAX_LLEN];
    } attrlist;
    struct {
        int extc;
        RDB_virtual_attr extv[DURO_MAX_LLEN];
    } extlist;
    struct {
        int addc;
        RDB_summarize_add addv[DURO_MAX_LLEN];
    } addlist;
}

%token <exp> ID
%token <exp> INTEGER
%token <exp> STRING
%token <exp> DECIMAL
%token <exp> FLOAT
%token <exp> TRUE
%token <exp> FALSE
%token WHERE
%token UNION
%token INTERSECT
%token MINUS
%token JOIN
%token FROM
%token TUPLE
%token ALL_BUT
%token AS
%token RENAME
%token EXTEND
%token SUMMARIZE
%token PER
%token ADD
%token MATCHES
%token OR
%token AND
%token NOT
%token CONCAT
%token TOK_COUNT
%token TOK_COUNTD
%token TOK_SUM
%token TOK_SUMD
%token TOK_AVG
%token TOK_AVGD
%token TOK_MAX
%token TOK_MIN

%type <exp> extractor relation project select rename extend summarize
        expression or_expression and_expression not_expression
        primary_expression rel_expression add_expression mul_expression
        literal operator_invocation

%type <attrlist> attribute_name_list

%type <extlist> extend_add_list extend_add

%type <addlist> summarize_add summarize_add_list summary summary_type

%%

expression: or_expression { resultp = $1; }
        | extractor { resultp = $1; }
        | relation { resultp = $1; }
        | project { resultp = $1; }
        | select { resultp = $1; }
        | rename { resultp = $1; }
        | extend { resultp = $1; }
        | summarize { resultp = $1; }
        ;

extractor: ID FROM expression {
        }
        | TUPLE FROM expression {
        }
        ;

project: primary_expression '{' attribute_name_list '}' {
            RDB_table *tbp, *restbp;
            int ret;
            int i;

            tbp = expr_to_table($1);
            if (tbp == NULL)
            {
                yyerror("project: table expected\n");
                YYERROR;
            }
            ret = RDB_project(tbp, $3.attrc, $3.attrv, &restbp);
            for (i = 0; i < $3.attrc; i++)
                free($3.attrv[i]);
            if (ret != RDB_OK) {
                yyerror(RDB_strerror(ret));
                YYERROR;
            }
            $$ = RDB_expr_table(restbp);
            }
            | primary_expression '{' ALL_BUT attribute_name_list '}'
        ;

attribute_name_list: ID {
            $$.attrc = 1;
            $$.attrv[0] = RDB_dup_str($1->var.attr.name);
            RDB_drop_expr($1);
            }
            | attribute_name_list ',' ID {
            int i;

            /* Copy old attributes */
            for (i = 0; i < $1.attrc; i++)
                $$.attrv[i] = $1.attrv[i];
            $$.attrv[$1.attrc] = RDB_dup_str($3->var.attr.name);
            $$.attrc = $1.attrc + 1;
            RDB_drop_expr($3);
        }
        ;

select: primary_expression WHERE or_expression {
            RDB_table *tbp, *restbp;
            int ret;

            tbp = expr_to_table($1);
            if (tbp == NULL)
            {
                yyerror("select: table expected\n");
                YYERROR;
            }
            ret = RDB_select(tbp, $3, &restbp);
            if (ret != RDB_OK) {
                yyerror(RDB_strerror(ret));
                YYERROR;
            }
            $$ = RDB_expr_table(restbp);
        }
        ;

rename: primary_expression RENAME '(' renaming_list ')'
        ;

renaming_list: renaming
        | renaming_list ',' renaming
        ;

renaming: ID AS ID
        | "PREFIX" STRING AS STRING
        | "SUFFIX" STRING AS STRING
        ;

relation: primary_expression UNION primary_expression {
            int ret;
            RDB_table *restbp, *tb1p, *tb2p;

            tb1p = expr_to_table($1);
            if (tb1p == NULL) {
                YYERROR;
            }
            tb2p = expr_to_table($3);
            if (tb2p == NULL) {
                YYERROR;
            }

            ret = RDB_union(tb1p, tb2p, &restbp);
            if (ret != RDB_OK) {
                yyerror(RDB_strerror(ret));
                YYERROR;
            }
            $$ = RDB_expr_table(restbp);
            RDB_drop_expr($1);
            RDB_drop_expr($3);
        }
        | primary_expression INTERSECT primary_expression {
            int ret;
            RDB_table *restbp, *tb1p, *tb2p;

            tb1p = expr_to_table($1);
            if (tb1p == NULL) {
                YYERROR;
            }
            tb2p = expr_to_table($3);
            if (tb2p == NULL) {
                YYERROR;
            }

            ret = RDB_intersect(tb1p, tb2p, &restbp);
            if (ret != RDB_OK) {
                yyerror(RDB_strerror(ret));
                YYERROR;
            }
            $$ = RDB_expr_table(restbp);
            RDB_drop_expr($1);
            RDB_drop_expr($3);
        }
        | primary_expression MINUS primary_expression {
            int ret;
            RDB_table *restbp, *tb1p, *tb2p;

            tb1p = expr_to_table($1);
            if (tb1p == NULL) {
                YYERROR;
            }
            tb2p = expr_to_table($3);
            if (tb2p == NULL) {
                YYERROR;
            }

            ret = RDB_minus(tb1p, tb2p, &restbp);
            if (ret != RDB_OK) {
                yyerror(RDB_strerror(ret));
                YYERROR;
            }
            $$ = RDB_expr_table(restbp);
            RDB_drop_expr($1);
            RDB_drop_expr($3);
        }
        | primary_expression JOIN primary_expression {
            int ret;
            RDB_table *restbp, *tb1p, *tb2p;

            tb1p = expr_to_table($1);
            if (tb1p == NULL) {
                YYERROR;
            }
            tb2p = expr_to_table($3);
            if (tb2p == NULL) {
                YYERROR;
            }

            ret = RDB_join(tb1p, tb2p, &restbp);
            if (ret != RDB_OK) {
                yyerror(RDB_strerror(ret));
                YYERROR;
            }
            $$ = RDB_expr_table(restbp);
            RDB_drop_expr($1);
            RDB_drop_expr($3);
        }
        ;

extend: EXTEND primary_expression ADD '(' extend_add_list ')' {
            RDB_table *tbp, *restbp;
            int ret;
            int i;

            tbp = expr_to_table($2);
            if (tbp == NULL)
            {
                yyerror("select: table expected\n");
                YYERROR;
            }
            ret = RDB_extend(tbp, $5.extc, $5.extv, &restbp);
            for (i = 0; i < $5.extc; i++) {
                free($5.extv[i].name);
                /* Expressions are not dropped since they are now owned
                 * by the new table.
                 */
            }
            if (ret != RDB_OK) {
                yyerror(RDB_strerror(ret));
                YYERROR;
            }
            $$ = RDB_expr_table(restbp);
        }
        ;

extend_add_list: extend_add {
            $$.extv[0].exp = $1.extv[0].exp;
            $$.extv[0].name = $1.extv[0].name;
            $$.extc = 1;
        }
        | extend_add_list ',' extend_add {
            int i;

            /* Copy old attributes */
            for (i = 0; i < $1.extc; i++) {
                $$.extv[i].name = $1.extv[i].name;
                $$.extv[i].exp = $1.extv[i].exp;
            }
            /* Add new attribute */
            $$.extv[$1.extc].name = $3.extv[0].name;
            $$.extv[$1.extc].exp = $3.extv[0].exp;
        
            $$.extc = $1.extc + 1;
        }
        ;

extend_add: expression AS ID {
            $$.extv[0].name = RDB_dup_str($3->var.attr.name);
            $$.extv[0].exp = $1;
        }
        ;

summarize: SUMMARIZE primary_expression PER expression
           ADD '(' summarize_add_list ')' {
            RDB_table *tb1p, *tb2p, *restbp;
            int ret;
            int i;

            tb1p = expr_to_table($2);
            if (tb1p == NULL)
            {
                yyerror("summarize: table expected\n");
                YYERROR;
            }

            tb2p = expr_to_table($4);
            if (tb2p == NULL)
            {
                yyerror("summarize: table expected\n");
                YYERROR;
            }

            ret = RDB_summarize(tb1p, tb2p, $7.addc, $7.addv, &restbp);
            for (i = 0; i < $7.addc; i++) {
                free($7.addv[i].name);
            }
            if (ret != RDB_OK) {
                for (i = 0; i < $7.addc; i++) {
                    RDB_drop_expr($7.addv[i].exp);
                }
                yyerror(RDB_strerror(ret));
                YYERROR;
            }
            $$ = RDB_expr_table(restbp);
        }
        ;

summarize_add_list: summarize_add {
            $$.addv[0].op = $1.addv[0].op;
            $$.addv[0].exp = $1.addv[0].exp;
            $$.addv[0].name = $1.addv[0].name;
            $$.addc = 1;
        }
        | summarize_add_list ',' summarize_add {
            int i;

            /* Copy old elements */
            for (i = 0; i < $1.addc; i++) {
                $$.addv[i].op = $1.addv[i].op;
                $$.addv[i].name = $1.addv[i].name;
                $$.addv[i].exp = $1.addv[i].exp;
            }

            /* Add new element */
            $$.addv[i].op = $3.addv[0].op;
            $$.addv[$1.addc].name = $3.addv[0].name;
            $$.addv[$1.addc].exp = $3.addv[0].exp;
        
            $$.addc = $1.addc + 1;
        }
        ;

summarize_add: summary AS ID {
            $$.addv[0].op = $1.addv[0].op;
            $$.addv[0].exp = $1.addv[0].exp;
            $$.addv[0].name = RDB_dup_str($3->var.attr.name);
            RDB_drop_expr($3);
        }
        ;

summary: TOK_COUNT {
            $$.addv[0].op = RDB_COUNT;
        }
        | TOK_COUNTD {
            $$.addv[0].op = RDB_COUNT;
        }
        | summary_type '(' expression ')' {
            $$.addv[0].op = $1.addv[0].op;
            $$.addv[0].exp = $3;
        }
        ;

summary_type: TOK_SUM {
            $$.addv[0].op = RDB_SUM;
        }
        | TOK_SUMD {
            $$.addv[0].op = RDB_SUMD;
        }
        | TOK_AVG {
            $$.addv[0].op = RDB_AVG;
        }
        | TOK_AVGD {
            $$.addv[0].op = RDB_AVGD;
        }
        | TOK_MAX {
            $$.addv[0].op = RDB_MAX;
        }
        | TOK_MIN {
            $$.addv[0].op = RDB_MIN;
        }
        ;

or_expression: and_expression
        | or_expression OR and_expression {
            $$ = RDB_or($1, $3);
            if ($$ == NULL)
                YYERROR;
        }
        ;

and_expression: not_expression
        | and_expression AND not_expression {
            $$ = RDB_and($1, $3);
            if ($$ == NULL)
                YYERROR;
        }
        ;

not_expression: rel_expression
        | NOT rel_expression {
            $$ = RDB_not($2);
            if ($$ == NULL)
                YYERROR;
        }
        ;

rel_expression: add_expression
        | add_expression '=' add_expression {
            $$ = RDB_eq($1, $3);
            if ($$ == NULL)
                YYERROR;
        }
        | add_expression "<>" add_expression {
            $$ = RDB_neq($1, $3);
            if ($$ == NULL)
                YYERROR;
        }
        | add_expression ">=" add_expression {
            $$ = RDB_get($1, $3);
            if ($$ == NULL)
                YYERROR;
        }
        | add_expression "<=" add_expression {
            $$ = RDB_let($1, $3);
            if ($$ == NULL)
                YYERROR;
        }
        | add_expression '>' add_expression {
            $$ = RDB_gt($1, $3);
            if ($$ == NULL)
                YYERROR;
        }
        | add_expression '<' add_expression {
            $$ = RDB_lt($1, $3);
            if ($$ == NULL)
                YYERROR;
        }
        | add_expression "IN" add_expression
        | add_expression MATCHES add_expression {
            $$ = RDB_regmatch($1, $3);
            if ($$ == NULL)
                YYERROR;
        }
        ;

add_expression: mul_expression
        | '+' mul_expression {
            $$ = $2;
        }
        | '-' mul_expression {
            /* !! */
        }
        | add_expression '+' mul_expression {
            $$ = RDB_add($1, $3);
            if ($$ == NULL)
                YYERROR;
        }
        | add_expression '-' mul_expression {
            $$ = RDB_subtract($1, $3);
            if ($$ == NULL)
                YYERROR;
        }
        | add_expression CONCAT mul_expression {
            $$ = RDB_concat($1, $3);
            if ($$ == NULL)
                YYERROR;
        }            
        ;

mul_expression: primary_expression
        | mul_expression '*' primary_expression {
            $$ = RDB_multiply($1, $3);
            if ($$ == NULL)
                YYERROR;
        }
        | mul_expression '/' primary_expression {
            $$ = RDB_divide($1, $3);
            if ($$ == NULL)
                YYERROR;
        }
        ;

primary_expression: ID
        | literal
        | operator_invocation
        | '(' expression ')' {
            $$ = $2;
        }
        ;

operator_invocation: ID '(' opt_argument_list ')'
        ;

opt_argument_list:
        | argument_list
        ;

argument_list: expression
        | argument_list ',' expression
        ;

literal: "RELATION" '{' expression_list '}' {
        }
        | "RELATION" '{' attribute_name_type_list '}'
          '{' opt_expression_list '}' {
        }
        | "RELATION" '{' '}'
          '{' opt_expression_list '}' {
        }
        | TUPLE '{' opt_tuple_item_list '}' {
        }
        | "TABLE_DEE" {
        }
        | "TABLE_DUM" {
        }
        | STRING
        | INTEGER
        | DECIMAL
        | FLOAT
        | TRUE
        | FALSE
        ;

opt_tuple_item_list:
        | tuple_item_list
        ;

tuple_item_list: tuple_item
        | tuple_item_list ',' tuple_item
        ;

tuple_item: ID expression
        ;

attribute_name_type_list: attribute_name_type
        | attribute_name_type_list ',' attribute_name_type
        ;

attribute_name_type: ID type
        ;

type: ID
        | "SAME_TYPE_AS" '(' expression ')'
        | "RELATION" '{' attribute_name_type_list '}'
        | "RELATION" '{' '}'
        | TUPLE '{' attribute_name_type_list '}'
        | TUPLE '{' '}'      
        ;

opt_expression_list:
        | expression_list
        ;

expression_list: expression
        | expression_list ',' expression
        ;

%%

static RDB_table *
expr_to_table (const RDB_expression *exp)
{
    int ret;

    if (exp->kind == RDB_TABLE)
        return exp->var.tbp;
    if (exp->kind == RDB_ATTR) {
        RDB_table *tbp;

        ret = RDB_get_table(exp->var.attr.name, expr_txp, &tbp);
        if (ret != RDB_OK)
            return NULL;
        return tbp;
    }
    return NULL;
}
