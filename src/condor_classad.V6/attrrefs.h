/*********************************************************************
 *
 * Condor ClassAd library
 * Copyright (C) 1990-2003, Condor Team, Computer Sciences Department,
 * University of Wisconsin-Madison, WI and Rajesh Raman.
 *
 * This source code is covered by the Condor Public License, which can
 * be found in the accompanying LICENSE file, or online at
 * www.condorproject.org.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * AND THE UNIVERSITY OF WISCONSIN-MADISON "AS IS" AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY, OF SATISFACTORY QUALITY, AND FITNESS
 * FOR A PARTICULAR PURPOSE OR USE ARE DISCLAIMED. THE COPYRIGHT
 * HOLDERS AND CONTRIBUTORS AND THE UNIVERSITY OF WISCONSIN-MADISON
 * MAKE NO MAKE NO REPRESENTATION THAT THE SOFTWARE, MODIFICATIONS,
 * ENHANCEMENTS OR DERIVATIVE WORKS THEREOF, WILL NOT INFRINGE ANY
 * PATENT, COPYRIGHT, TRADEMARK, TRADE SECRET OR OTHER PROPRIETARY
 * RIGHT.
 *
 *********************************************************************/

#ifndef __ATTRREFS_H__
#define __ATTRREFS_H__

BEGIN_NAMESPACE( classad )

/// Represents a attribute reference node (like .b) in the expression tree
class AttributeReference : public ExprTree 
{
  	public:

        /// Copy Constructor
        AttributeReference(const AttributeReference &ref);

		///  Destructor
    	~AttributeReference ();

        /// Assignment operator
        AttributeReference &operator=(const AttributeReference &ref);

		/** Factory method to create attribute reference nodes.
			@param expr The expression part of the reference (i.e., in
				case of expr.attr).  This parameter is NULL if the reference
				is absolute (i.e., .attr) or simple (i.e., attr).
			@param attrName The name of the reference.  This string is
				duplicated internally.
			@param absolute True if the reference is an absolute reference
				(i.e., in case of .attr).  This parameter cannot be true if
				expr is not NULL
		*/
    	static AttributeReference *MakeAttributeReference(ExprTree *expr, 
					const std::string &attrName, bool absolute=false);

		/** Deconstructor to get the components of an attribute reference
		 * 	@param expr The expression part of the reference (NULL for
		 * 		absolute or simple references)
		 * 	@param attr The name of the attribute being referred to
		 * 	@param abs  true iff the reference is absolute (i.e., .attr)
		 */
		void GetComponents( ExprTree *&expr,std::string &attr, bool &abs ) const;

        /** Return a copy of this attribute reference.
         */
		virtual ExprTree* Copy( ) const;

        /** Copy from the given reference into this reference.
         *  @param ref The reference to copy from.
         *  @return true if the copy succeeded, false otherwise.
         */
        bool CopyFrom(const AttributeReference &ref);

        /** Is this attribute reference the same as another?
         *  @param tree The reference to compare with
         *  @return true if they are the same, false otherwise.
         */
        virtual bool SameAs(const ExprTree *tree) const;

        /** Are the two attribute references the same?
         *  @param ref1 An attribute reference
         *  @param ref2 Another attribute reference
         *  @return true if they are the same, false otherwise.
         */
        friend bool operator==(const AttributeReference &ref1, const AttributeReference &ref2);

	protected:
		/// Constructor
    	AttributeReference ();

  	private:
		// private ctor for internal use
		AttributeReference( ExprTree*, const std::string &, bool );
		virtual void _SetParentScope( const ClassAd* p );
    	virtual bool _Evaluate( EvalState & , Value & ) const;
    	virtual bool _Evaluate( EvalState & , Value &, ExprTree*& ) const;
    	virtual bool _Flatten( EvalState&, Value&, ExprTree*&, int* ) const;
		int	FindExpr( EvalState&, ExprTree*&, ExprTree*&, bool ) const;

		ExprTree	*expr;
		bool		absolute;
    	std::string attributeStr;
};

END_NAMESPACE // classad

#endif//__ATTRREFS_H__
