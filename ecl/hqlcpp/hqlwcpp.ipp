/*##############################################################################

    Copyright (C) 2011 HPCC Systems.

    All rights reserved. This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU Affero General Public License as
    published by the Free Software Foundation, either version 3 of the
    License, or (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU Affero General Public License for more details.

    You should have received a copy of the GNU Affero General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
############################################################################## */
#ifndef HQLWCPP_IPP
#define HQLWCPP_IPP

class HQLCPP_API CppWriterTemplate : public CInterface, public ITemplateExpander
{
public:
    CppWriterTemplate();
    ~CppWriterTemplate();
    IMPLEMENT_IINTERFACE

    virtual void generate(ISectionWriter & writer, unsigned pass, IProperties * properties = NULL);
                    bool loadTemplate(const char * filename, const char *dir);

    void outputQuoted(ISectionWriter & writer, size32_t len, const char * str)
    {
        writer.noteLines(memcount(len, str, '\n'));
        outStream->write(len, str);
    }

    void setOutput(IFile * _output)
    {
        Owned<IFileIO> io = _output->open(IFOcreate);
        if (!io)
            throwError1(HQLERR_CouldNotCreateOutputX, _output->queryFilename());

        out.set(_output);
        outStream.setown(createIOStream(io));
    }


private:
  enum TplSectionType { TplEmbed, TplExpand, TplCondition, TplEndCondition };
    struct CppTemplateSection : public CInterface
    {
        TplSectionType  type;
        _ATOM                       id;
        const char *        position;
        unsigned                len;
        unsigned                indent;
    };

protected:
    char * text;
    unsigned len;
    CIArray sections;
    Owned<IFile> out;
    Owned<IIOStream> outStream;
};

class HQLCPP_API HqlCppWriter
{
public:
    HqlCppWriter(CompilerType _compiler);
    HqlCppWriter(StringBuffer & _out, CompilerType _compiler);

    void generateType(ITypeInfo * type, const char * name);
    StringBuffer & generateExprCpp(IHqlExpression * expr);
    void generateStatementsForPass(HqlStmts & stmts, unsigned indent, unsigned pass);
    void noteLines(size32_t count) { outputLineNum += count; }
    void setOutput(IFile * out, IIOStream * outStream); 

protected:
    void flush();
    void generate(HqlStmtArray & stmts);
    void generateChildren(IHqlStmt * stmt, bool addBraces);
    StringBuffer & generateChildExpr(StringBuffer & out, IHqlExpression * expr, unsigned childIndex);
    void generateInitializer(IHqlExpression * expr);
    void generateParamCpp(IHqlExpression * expr);
    void generateSimpleAssign(IHqlExpression * target, IHqlExpression * source);
    void generateStmt(IHqlStmt * stmt);
    void generateStmtAssign(IHqlStmt * assign);
    void generateStmtAssignModify(IHqlStmt * assign);
    void generateStmtCase(IHqlStmt * stmt);
    void generateStmtDeclare(IHqlStmt * declare);
    void generateStmtFilter(IHqlStmt * stmt);
    void generateStmtLine(IHqlStmt * stmt);
    void generateStmtLoop(IHqlStmt * stmt);
    void generateStmtSwitch(IHqlStmt * stmt);
    void generateStmtForPass(IHqlStmt * stmt, unsigned pass);
    StringBuffer & indent();
    void indent(int delta)          { curIndent += delta; }
    StringBuffer & newline();
    StringBuffer & queryBreakLine();
    StringBuffer & queryIndent();
    void queryNewline();

    StringBuffer & generateChildExpr(IHqlExpression * expr, unsigned childIndex);
    StringBuffer & generateCommaChildren(IHqlExpression * expr);
    void generateOrderExpr(IHqlExpression * left, IHqlExpression * right);
    StringBuffer & generateExprAsChar(IHqlExpression * expr);

protected:
    Linked<IFile> targetFile;
    Linked<IIOStream> target;
    StringBuffer & out;
    StringBuffer defaultOut;
    unsigned curIndent;
    unsigned startOffset;
    unsigned outputLineNum;
    CompilerType compiler;
};

class HQLCPP_API HqlCppSectionWriter : public CInterface, implements ISectionWriter
{
public:
    HqlCppSectionWriter(IHqlCppInstance & _instance, CompilerType _compiler) : instance(_instance), writer(_compiler)
    {
    }
    IMPLEMENT_IINTERFACE

    virtual void generateSection(unsigned indent, _ATOM section, unsigned pass);
    virtual void noteLines(size32_t count) { writer.noteLines(count); }
    virtual void setOutput(IFile * out, IIOStream * outStream) { writer.setOutput(out, outStream); }

    IHqlCppInstance &   instance;
    HqlCppWriter writer;
};

#endif
