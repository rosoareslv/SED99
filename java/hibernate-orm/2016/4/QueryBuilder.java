/*
 * Hibernate, Relational Persistence for Idiomatic Java
 *
 * License: GNU Lesser General Public License (LGPL), version 2.1 or later.
 * See the lgpl.txt file in the root directory or <http://www.gnu.org/licenses/lgpl-2.1.html>.
 */
package org.hibernate.envers.internal.tools.query;

import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Map;

import org.hibernate.Query;
import org.hibernate.Session;
import org.hibernate.envers.RevisionType;
import org.hibernate.envers.internal.entities.RevisionTypeType;
import org.hibernate.envers.internal.tools.MutableInteger;
import org.hibernate.envers.internal.tools.StringTools;
import org.hibernate.envers.internal.tools.Triple;
import org.hibernate.type.CustomType;

/**
 * A class for incrementally building a HQL query.
 *
 * @author Adam Warski (adam at warski dot org)
 */
public class QueryBuilder {
	private final String entityName;
	private final String alias;

	/**
	 * For use by alias generator (in case an alias is not provided by the user).
	 */
	private final MutableInteger aliasCounter;
	/**
	 * For use by parameter generator, in {@link Parameters}. This counter must be
	 * the same in all parameters and sub-queries of this query.
	 */
	private final MutableInteger paramCounter;
	/**
	 * "where" parameters for this query. Each parameter element of the list for one alias from the "from" part.
	 */
	private final List<Parameters> parameters = new ArrayList<Parameters>();

	/**
	 * A list of triples (from entity name, alias name, whether to select the entity).
	 */
	private final List<Triple<String, String, Boolean>> froms;
	/**
	 * A list of triples (alias, property name, order ascending?).
	 */
	private final List<Triple<String, String, Boolean>> orders;
	/**
	 * A list of complete projection definitions: either a sole property name, or a function(property name).
	 */
	private final List<String> projections;

	/**
	 * @param entityName Main entity which should be selected.
	 * @param alias Alias of the entity
	 */
	public QueryBuilder(String entityName, String alias) {
		this( entityName, alias, new MutableInteger(), new MutableInteger() );
	}

	private QueryBuilder(String entityName, String alias, MutableInteger aliasCounter, MutableInteger paramCounter) {
		this.entityName = entityName;
		this.alias = alias;
		this.aliasCounter = aliasCounter;
		this.paramCounter = paramCounter;

		final Parameters rootParameters = new Parameters( alias, "and", paramCounter );
		parameters.add( rootParameters );

		froms = new ArrayList<Triple<String, String, Boolean>>();
		orders = new ArrayList<Triple<String, String, Boolean>>();
		projections = new ArrayList<String>();

		addFrom( entityName, alias, true );
	}

	// Only for deep copy purpose.
	private QueryBuilder(QueryBuilder other) {
		this.entityName = other.entityName;
		this.alias = other.alias;
		this.aliasCounter = other.aliasCounter.deepCopy();
		this.paramCounter = other.paramCounter.deepCopy();
		for (final Parameters params : other.parameters) {
			this.parameters.add( params.deepCopy() );
		}

		froms = new ArrayList<Triple<String, String, Boolean>>( other.froms );
		orders = new ArrayList<Triple<String, String, Boolean>>( other.orders );
		projections = new ArrayList<String>( other.projections );
	}

	public QueryBuilder deepCopy() {
		return new QueryBuilder( this );
	}

	/**
	 * @return the main alias of this query builder
	 */
	public String getAlias() {
		return alias;
	}

	/**
	 * Add an entity from which to select.
	 *
	 * @param entityName Name of the entity from which to select.
	 * @param alias Alias of the entity. Should be different than all other aliases.
	 * @param select whether the entity should be selected
	 */
	public void addFrom(String entityName, String alias, boolean select) {
		froms.add( Triple.make( entityName, alias, select ) );
	}

	public String generateAlias() {
		return "_e" + aliasCounter.getAndIncrease();
	}

	/**
	 * @return A sub-query builder for the same entity (with an auto-generated alias). The sub-query can
	 *         be later used as a value of a parameter.
	 */
	public QueryBuilder newSubQueryBuilder() {
		return newSubQueryBuilder( entityName, generateAlias() );
	}

	/**
	 * @param entityName Entity name, which will be the main entity for the sub-query.
	 * @param alias Alias of the entity, which can later be used in parameters.
	 *
	 * @return A sub-query builder for the given entity, with the given alias. The sub-query can
	 *         be later used as a value of a parameter.
	 */
	public QueryBuilder newSubQueryBuilder(String entityName, String alias) {
		return new QueryBuilder( entityName, alias, aliasCounter, paramCounter );
	}

	public Parameters getRootParameters() {
		return parameters.get( 0 );
	}

	public Parameters addParameters(final String alias) {
		final Parameters result = new Parameters( alias, Parameters.AND, paramCounter);
		parameters.add( result );
		return result;
	}

	public void addOrder(String alias, String propertyName, boolean ascending) {
		orders.add( Triple.make( alias, propertyName, ascending ) );
	}

	public void addProjection(String function, String alias, String propertyName, boolean distinct) {
		final String effectivePropertyName = propertyName == null ? "" : ".".concat( propertyName );
		if ( function == null ) {
			projections.add( (distinct ? "distinct " : "") + alias + effectivePropertyName );
		}
		else {
			projections.add(
					function + "(" + (distinct ? "distinct " : "") + alias +
					effectivePropertyName + ")"
			);
		}
	}

	/**
	 * Builds the given query, appending results to the given string buffer, and adding all query parameter values
	 * that are used to the map provided.
	 *
	 * @param sb String builder to which the query will be appended.
	 * @param queryParamValues Map to which name and values of parameters used in the query should be added.
	 */
	public void build(StringBuilder sb, Map<String, Object> queryParamValues) {
		sb.append( "select " );
		if ( projections.size() > 0 ) {
			// all projections separated with commas
			StringTools.append( sb, projections.iterator(), ", " );
		}
		else {
			// all aliases separated with commas
			StringTools.append( sb, getSelectAliasList().iterator(), ", " );
		}
		sb.append( " from " );
		// all from entities with aliases, separated with commas
		StringTools.append( sb, getFromList().iterator(), ", " );
		// where part - rootParameters
		boolean first = true;
		for (final Parameters params : parameters) {
			if (!params.isEmpty()) {
				if (first) {
					sb.append( " where " );
					first = false;
				}
				else {
					sb.append( " and " );
				}
				params.build( sb, queryParamValues );
			}
		}
		// orders
		if ( orders.size() > 0 ) {
			sb.append( " order by " );
			StringTools.append( sb, getOrderList().iterator(), ", " );
		}
	}

	private List<String> getSelectAliasList() {
		final List<String> aliasList = new ArrayList<String>();
		for ( Triple<String, String, Boolean> from : froms ) {
			if ( from.getThird() ) {
				aliasList.add( from.getSecond() );
			}
		}

		return aliasList;
	}

	public String getRootAlias() {
		return alias;
	}

	private List<String> getFromList() {
		final List<String> fromList = new ArrayList<String>();
		for ( Triple<String, String, Boolean> from : froms ) {
			fromList.add( from.getFirst() + " " + from.getSecond() );
		}

		return fromList;
	}

	private List<String> getOrderList() {
		final List<String> orderList = new ArrayList<String>();
		for ( Triple<String, String, Boolean> order : orders ) {
			orderList.add( order.getFirst() + "." + order.getSecond() + " " + (order.getThird() ? "asc" : "desc") );
		}

		return orderList;
	}

	public Query toQuery(Session session) {
		final StringBuilder querySb = new StringBuilder();
		final Map<String, Object> queryParamValues = new HashMap<String, Object>();

		build( querySb, queryParamValues );

		final Query query = session.createQuery( querySb.toString() );
		for ( Map.Entry<String, Object> paramValue : queryParamValues.entrySet() ) {
			if ( paramValue.getValue() instanceof RevisionType ) {
				// this is needed when the ClassicQueryTranslatorFactory is used
				query.setParameter(
						paramValue.getKey(),
						paramValue.getValue(),
						new CustomType( new RevisionTypeType() )
				);
			}
			else {
				query.setParameter( paramValue.getKey(), paramValue.getValue() );
			}
		}
		return query;
	}
}
