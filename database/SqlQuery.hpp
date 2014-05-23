#ifndef SQL_QUERY_HPP___
#define SQL_QUERY_HPP___

#include <list>
#include <string>


class WhereClause
{
	public:

		WhereClause() {}
		WhereClause(const std::string& clause) { _clause = clause; }

		WhereClause& And(const WhereClause& clause);
		WhereClause& Or(const WhereClause& clause);

		// Arguments binding (for each '?' in where clause)
		WhereClause& bind(const std::string& arg);

		std::string get() const;
		const std::list<std::string>&	getBindArgs(void) const	{return _bindArgs;}

	private:

		std::string _clause;		// WHERE clause
		std::list<std::string>  _bindArgs;

};

class InnerJoinClause
{
	public:

		InnerJoinClause() {}
		InnerJoinClause(const std::string& clause);

		InnerJoinClause&	And(const InnerJoinClause& clause);
		std::string get() const	{ return _clause;}

	private:

		std::string _clause;
};

class GroupByStatement
{
	public:
		GroupByStatement() {}
		GroupByStatement(const std::string& statement) { _statement = statement; }

		GroupByStatement& And(const GroupByStatement& statement);

		std::string get() const      {return _statement;}

	private:
		std::string _statement;		// SELECT statement

};

class SelectStatement
{
	public:
		SelectStatement() {};
		SelectStatement(const std::string& item);

		SelectStatement& And(const std::string& item);

		std::string get() const;

	private:

		std::list<std::string>	_statement;
};

class FromClause
{
	public:

		FromClause() {}
		FromClause(const std::string& clause);

		FromClause& And(const FromClause& clause);

		std::string get() const;

	private:

		std::list<std::string>	_clause;

};

class SqlQuery
{
	public:

		SelectStatement&	select(void)		{ return _selectStatement;}
		SelectStatement&	select(const std::string& statement)		{ _selectStatement = SelectStatement(statement); return _selectStatement; }
		FromClause&		from(void)		{ return _fromClause; }
		FromClause&		from(const std::string& clause)			{ _whereClause = WhereClause(clause); return _fromClause; }
		InnerJoinClause&	innerJoin(void)		{ return _innerJoinClause; }
		WhereClause&		where(void)		{ return _whereClause; }
		const WhereClause&	where(void) const	{ return _whereClause; }
		GroupByStatement&	groupBy(void) 		{ return _groupByStatement; }
		const GroupByStatement&	groupBy(void) const	{ return _groupByStatement; }

		std::string get(void) const;

	private:

		SelectStatement		_selectStatement;	// SELECT statement
		InnerJoinClause		_innerJoinClause;	// INNER JOIN
		FromClause		_fromClause;		// FROM tables
		WhereClause		_whereClause;		// WHERE clause
		GroupByStatement	_groupByStatement;	// GROUP BY statement
};

#endif

