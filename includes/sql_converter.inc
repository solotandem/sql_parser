<?php
// $Id: sql_converter.inc,v 1.13 2010/09/04 19:11:05 duellj Exp $

/**
 * @file
 */

/**
 * Sql Convert Base Class
 *
 * Provides common converting methods.
 */
class SqlConverter {
  /**
   * The sql object generated by SqlParser.
   *
   * @param SqlObject $sql_object
   */
  protected $sql_object;

  /**
   * The arguments used to replace placeholder fields.
   *
   * @param array $arguments
   */
  protected $arguments = array();

  /**
   * An array of new placeholders.  Used to check for duplicates
   *
   * @param array.
   */
  protected $placeholders = array();

  public function __construct(SqlObject $sql_object) {
    $this->sql_object = $sql_object;
  }

  /**
   * Sets the query arguments.
   *
   * Any placeholders within the SQL statement correspond to arguments on a
   * sequential basis, i.e. the first placeholder found (usually within the
   * conditionals) will correspond to the first element in the arguments
   * array.
   *
   * @param $arguments
   *   An array of placeholder arguments.
   */
  public function setQueryArguments($arguments) {
    $this->arguments = $arguments;
  }

  /**
   * Returns the query arguments.
   *
   * @return
   *   An array of placeholder arguments.
   */
  public function getQueryArguments() {
    return $this->arguments;
  }

  /**
   * Searches and converts placeholders for conditional statements.
   *
   * @param SqlConditional
   *   The SqlConditional object.
   */
  protected function convertConditionalPlaceholders(SqlConditional &$conditional) {
sdp(__FUNCTION__);
sdp($conditional, '$conditional');
    // @todo This is limited to two conditions.
    if ($conditional->arg1 instanceof SqlConditional) {
      $this->convertConditionalPlaceholders($conditional->arg1);
    }
    if ($conditional->arg2 instanceof SqlConditional) {
      $this->convertConditionalPlaceholders($conditional->arg2);
    }

    if ($conditional->arg1 instanceof SqlField) {
      if ($conditional->arg2->type == 'placeholder') {
        $conditional->arg2->name = $this->convertPlaceholder($conditional->arg1);
      }
      elseif ($conditional->arg2->type != 'identifier') {
        // Convert literal conditions into placeholders.
        array_splice($this->arguments, count($this->placeholders), 0, $conditional->arg2->name);
        $conditional->arg2->name = $this->convertPlaceholder($conditional->arg1);
      }
    }
  }

  /**
   * Converts Drupal 6 style placeholders (e.g. %s) into Drupal 7 style placeholders.
   *
   * @param SqlField
   *   The field which the placeholder is being compared to.  Used to generate a 
   *   new placeholder name.
   *
   * @return
   *   The new placeholder name.
   */
  protected function convertPlaceholder(SqlField $field) {
// sdp(__FUNCTION__);
// sdp($field, '$field');
    if ($field->alias) {
      $name = $field->alias;
    }
    else {
      $name = '';
      if ($field->table) {
        $name = $field->table . '.';
      }
      $name .= $field->name;
    }

    $original_name = $name = ':' . str_replace('.', '_', $name);

    $i = 1;
    while (in_array($name, $this->placeholders)) {
      $name = $original_name . '_' . $i;
      $i++;
    }
    $this->placeholders[] = $name;
    return $name;
  }

  /**
   * Pops the next item off the arguments array or returns todo string.
   *
   * @return
   *   The next item off the arguments array or the default todo string.
   */
  protected function getArgument() {
    return !empty($this->arguments) ? array_shift($this->arguments) : "'@todo Set the appropriate argument.'";
  }

  /**
   * Creates an array string using placeholders as keys and arguments as values.
   *
   * @return string
   *   The placeholder and argument array as a string.
   */
  public function getPlaceholderArray() {
    $string = '';
    if (!empty($this->placeholders)) {
      $new_values = array();
      $count = count($this->placeholders);
      foreach ($this->placeholders as $placeholder) {
        $new_values[] = "'" . $placeholder . "' => " . $this->getArgument();
      }
      $string = 'array(' . implode(', ', $new_values) . ')';
    }
    return $string;
  }
}

/**
 * SQL DBTNG Converter
 *
 * Takes a parsed sql array and converts it into a DBTNG object string.
 */
class SqlDBTNGConverter extends SqlConverter {

  protected $output = array();

  /**
   * Perform conversion.
   */
  public function convert() {
    // Remove {} from tables.
    $this->convertTables();

    switch ($this->sql_object->command) {
      case 'select':
        $this->convertSelect();
        break;

      case 'insert':
        $this->convertInsert();
        break;

      case 'update':
        $this->convertUpdate();
        break;

      case 'delete':
        $this->convertDelete();
        break;
    }
    $this->output[] = "  ->execute();";

    return join("\n", $this->output);
  }

  /**
   * Convert select query.
   */
  protected function convertSelect() {
    // Add first table to db_select() function.
    $this->output[] = "\$query = db_select('" . $this->sql_object->tables[0]->name . "', '" . $this->getTableAlias(0) . "')";

    $this->convertSelectFields();

    if (count($this->sql_object->tables) > 1) {
      $this->convertJoins();
    }

    if ($this->sql_object->conditional) {
      $this->convertConditionals($this->sql_object->conditional);
    }

    if ($this->sql_object->group_by_clause) {
      foreach ($this->sql_object->group_by_clause->columns as $group_by_column) {
        $this->output[] = "  ->groupBy($group_by_column)";
      }

      if (isset($this->sql_object->having_clause)) { // @todo Why do we need to check isset() on this but not other objects? Where are they defaultly set?
        $this->convertHaving($this->sql_object->having_clause);
      }
    }

    if ($this->sql_object->order_by_clauses) {
      foreach ($this->sql_object->order_by_clauses as $order) {
        $this->output[] = "  ->orderBy('" . $order->column . "', '" . $order->direction . "')";
      }
    }

    if ($this->sql_object->limit) {
      $this->output[] = "  ->range(" . $this->sql_object->limit->offset . ', ' . $this->sql_object->limit->row_count . ")";
    }
  }

  /**
   * Convert insert query.
   */
  protected function convertInsert() {
    $this->output[] = '$id = db_insert(\'' . $this->sql_object->tables[0]->name . '\')';
    $this->convertFields();
  }

  /**
   * Convert update query.
   */
  protected function convertUpdate() {
    $this->output[] = "db_update('" . $this->sql_object->tables[0]->name . "')";

    $this->convertFields();

    if ($this->sql_object->conditional) {
      $this->convertConditionals($this->sql_object->conditional);
    }

    if ($this->sql_object->order_by_clauses) {
      foreach ($this->sql_object->order_by_clauses as $order) {
        $this->output[] = "  ->orderBy('" . $order->column . "', '" . $order->direction . "')";
      }
    }

    if ($this->sql_object->limit) {
      $this->output[] = "  ->range(" . $this->sql_object->limit->offset . ', ' . $this->sql_object->limit->row_count . ")";
    }
  }

  /**
   * Convert delete query.
   */
  protected function convertDelete() {
    $this->output[] = "db_delete('" . $this->sql_object->tables[0]->name . "')";

    if ($this->sql_object->conditional) {
      $this->convertConditionals($this->sql_object->conditional);
    }
  }

  /**
   * Convert table definitions.
   *
   * Removes curly braces from table names.
   */
  protected function convertTables() {
    foreach ($this->sql_object->tables as $key => $table) {
      $table->name = str_replace(array('{', '}'), '', $table->name);
      $this->sql_object->tables[$key] = $table;
    }
  }

  /**
   * Convert Select query fields.
   */
  protected function convertSelectFields() {
sdp(__FUNCTION__);
sdp($this->sql_object->tables, '$this->sql_object->tables');
sdp($this->sql_object->fields, '$this->sql_object->fields');
    // Save first table items.
    $table0 = $this->sql_object->tables[0];
    // Select all fields.
    // @todo An asterisk may occur in other parts of the field list with a table qualifier.
    // Individual fields from a table may be added in addition to an asterisk on that table.
    if (FALSE && $this->sql_object->fields[0]->name == '*') {
      $this->output[] = "  ->fields()";
    }
    // Select individual fields.
    else {
      // Group fields by table.  If no table is set for a field, then use the first table.
      // If field has an alias, then the field needs to be grouped individually.
      $all_fields = $table_fields = $aliased_fields = array();
      foreach ($this->sql_object->fields as $id => $field) {
        if ($field->name == '*') {
          // If no table is set for a field, then use the first table.
          $table_alias = $field->table ? $field->table : ($table0->alias ? $table0->alias : $table0->name);
          // Always add to output in case individual fields from the table are also included.
          $this->output[] = "  ->fields('$table_alias')";
        }
        elseif ($field->alias) {
          $aliased_fields[] = $field;
        }
        else {
          if ($field->table) {
            $table_fields[$field->table][] = $field->name;
          }
          else {
            $table_fields[$this->sql_object->tables[0]->alias][] = $field->name;
          }
        }
      }

      // Add fields for each table.
      if ($table_fields) {
        foreach ($table_fields as $table_alias => $fields) {
          $this->output[] = "  ->fields('$table_alias', array('" . join("', '", $fields) . "'))";
        }
      }

      // Add aliased fields.
      if ($aliased_fields) {
        // End the current expression as addField() returns a field object not a
        // select query object.
        $this->output[count($this->output) - 1] .= ';';
        foreach ($aliased_fields as $field) {
//           if (!($table_alias = $field->table)) {
//             $table_alias = $this->sql_object->tables[0]->alias;
//           }
          // If no table is set for a field, then use the first table.
          $table_alias = $field->table ? $field->table : ($table0->alias ? $table0->alias : $table0->name);
          $this->output[] = "\$query->addField('$table_alias', '" . $field->name . "', '" . $field->alias . "');";
        }
      }
    }
  }

  /**
   * Convert Insert and Update query fields.
   */
  protected function convertFields() {
    $this->output[] = "  ->fields(array(";
    foreach ($this->sql_object->columns as $column) {
      if ($column->type == 'placeholder') {
        $value = $this->getArgument();
      }
      else {
        $value = $column->value;
      }
      $this->output[] = "    '" . $column->name . "' => " . $value . ",";
    }
    $this->output[] = "  ))";
  }

  /**
   * Convert table joins.
   *
   * As join() returns the unique alias that was assigned for this table, it
   * must form a separate statement.
   */
  function convertJoins() {
    foreach ($this->sql_object->tables as $key => $table) {
      // Reset the placeholders.
      // @todo Does this have side effects on the arguments passed to the function later?
      $this->placeholders = array();
      // Only convert tables that have a join.
      if ($table->join) {
        switch ($table->join) {
          case 'join':
          case 'inner join':
            $join_statement = '$query->join(';
            break;

          case 'left join':
            $join_statement = '$query->leftJoin(';
            break;

          case 'right join':
            $join_statement = '$query->rightJoin(';
            break;
        }
        $right_table_alias = $this->getTableAlias($key);
        $left_table_alias = $this->getTableAlias($key - 1);

        $join_statement .= "'" . $table->name . "', ";
        $join_statement .= "'" . $right_table_alias . "', ";

        // Add join condition.
        if ($table->join_conditional->type == 'using') {
          $join_conditions = array();
          foreach ($table->join_conditional->columns as $column) {
            // @todo Are the left and right column names always the same (in this code anyhow)?
            $join_conditions[] = $left_table_alias . '.' . $column . ' = ' . $right_table_alias . '.' . $column;
          }
          $join_statement .= "'" . join(' AND ', $join_conditions) . "'";
        }
        else {
          // Convert any placeholders and literals in the join conditional 
          // statement to Drupal 7 placeholders.  This will set the 
          // $this->placeholders array with the new placeholders, which can then 
          // be used to generate the arguments array.
          $this->convertConditionalPlaceholders($table->join_conditional);
          // @todo There's probably a better way to remove the trailing " ON " 
          // from the generated join conditional.
          $join_statement .= "'" . substr($table->join_conditional, 4) . "'";
          if (!empty($this->placeholders)) {
            $join_statement .= ', ' . $this->getPlaceholderArray();
          }
        }

        $join_statement .= ");";

        $this->output[] = $join_statement;
      }
    }
  }

  /**
   * Transform parsed sql into conditional object strings.
   *
   * @param SqlConditional $conditionals
   *   Parsed conditional object.
   * @param $level
   *   Level of conditional statements.
   */
  protected function convertConditionals(SqlConditional $conditional, $level = 1) {
    $closing_parenthesis = FALSE;

    // Start the statement (@todo only if previous expressions required separate statements).
    $this->output[] = '$query';

    // Convert operator.
    switch ($conditional->operator) {
      case 'and':
        if ($level > 1) {
          $this->output[] = str_repeat("  ", $level - 1) . "->condition(db_and()";
          $closing_parenthesis = TRUE;
        }
        break;

      case 'or':
        $this->output[] = str_repeat("  ", $level - 1) . "->condition(db_or()";
        $closing_parenthesis = TRUE;
        break;

      case 'xor':
        $this->output[] = str_repeat("  ", $level - 1) . "->condition(db_xor()";
        $closing_parenthesis = TRUE;
        break;
    }

    // Convert conjunction.
    if (in_array($conditional->operator, array('and', 'or', 'xor'))) {
      $this->convertConditionalFragment($conditional->arg1, $level);
      $this->convertConditionalFragment($conditional->arg2, $level);
    }
    // Convert single conditional fragment.
    else {
      $this->convertConditionalFragment($conditional, $level);
    }

    if ($closing_parenthesis) {
      $this->output[] = str_repeat("  ", $level - 1) . ")";
    }
  }

  /**
   * Transform conditional fragment into conditional object string.
   *
   * If conditional fragment is a subclause, pass it back to
   * coder_upgrade_convert_conditional.
   *
   * @param SqlConditional $conditional
   *   Parsed conditional object.
   * @param $level
   *   Level of conditional statements.
   */
  protected function convertConditionalFragment(SqlConditional $conditional, $level = 1) {
    if ($conditional->arg1 instanceof SqlConditional) {
      // Arg1 is conditional, pass back to convertConditional().  If operator 
      // is another AND, then the level doesn't need to be increased.
      $this->convertConditionals($conditional->arg1, $level);

      // Arg2 is conditional, pass back to convertConditional().
      if ($conditional->arg2 instanceof SqlConditional) {
        $this->convertConditionals($conditional->arg2, $level);
      }
    }
    else {
      // NULL values require special methods
      if ($conditional->arg2->type == 'null') {
        if ($conditional->not_operator) {
          $output = str_repeat("  ", $level) . "->isNotNull('" . $conditional->arg1 . "')";
        }
        else {
          $output = str_repeat("  ", $level) . "->isNull('" . $conditional->arg1 . "')";
        }
      }
      else {
        $output = str_repeat("  ", $level) . "->condition('" . $conditional->arg1;

        // Determine the correct arg2 for the conditional
        if ($conditional->arg2->type == 'placeholder') {
          $value = $this->getArgument();

          // IN operator now just takes an array, rather than imploded array.
          if ($conditional->operator == 'in' && preg_match("/db_placeholders\(([^,]*).*\)/", $value, $matches)) {
            $value = $matches[1];
          }
        }
        elseif ($conditional->operator == 'in' && is_array($conditional->arg2)) {
          $value = 'array(' . join(', ', $conditional->arg2) . ')';
        }
        else {
          $value = $conditional->arg2->name;
        }
        $output .=  "', $value";

        // Add operator if necessary.
        if ($conditional->operator != '=') {
          $output .= ", '" . $conditional->operator . "'";
        }

        $output .= ")";
      }
      $this->output[] = $output;
    }
  }

  /**
   * Transform a conditional object into an SQL string.
   *
   * Any placeholders will be rewritten to Drupal 7 placeholder style.  In order 
   * to handle nested conditionals, the placeholder count is passed for 
   * recursive calls.  In order to handle multiple types of calls, only the SQL 
   * string and, if needed, the placeholder array will be returned, e.g.:
   * @code
   *   'n.nid = :n_nid', array('n_nid' => '...')
   * @code
   *
   * @param SqlConditional $conditional
   *   Parsed conditional object.
   * @param $placeholders
   *   The number of placeholders found
   *
   * @return
   *   The converted SQL string.
   */
  protected function convertRawConditional(SqlConditional $conditional, &$placeholders = 0) {
    if ($conditional->arg1 instanceof SqlConditional) {
      $this->convertRawConditional($conditional->arg1, $placeholders);
    }
    // Convert placeholders
    if ($conditional->arg2->type == 'placeholder') {

    }
  }

  /**
   * Transform parsed having sql into having object strings.
   *
   * @param SqlHavingClause $having
   *   Parsed having object.
   * @param $values
   *   Placeholder values.
   * @param $level
   *   Level of having statements.
   *
   * @todo Refactor with convertCondtionals().
   */
  protected function convertHaving($having, &$values, $level = 1) {
    $closing_parenthesis = FALSE;

    // Convert operator.
    switch ($havings['op']) {
      case 'and':
        if ($level > 1) {
          $this->output[] = str_repeat("  ", $level - 1) . "->havingCondition(db_and()";
          $closing_parenthesis = TRUE;
        }
        break;

      case 'or':
        $this->output[] = str_repeat("  ", $level - 1) . "->havingCondition(db_or()";
        $closing_parenthesis = TRUE;
        break;

      case 'xor':
        $this->output[] = str_repeat("  ", $level - 1) . "->havingCondition(db_xort()";
        $closing_parenthesis = TRUE;
        break;
    }

    // Convert conjunction.
    if (in_array($havings['op'], array('and', 'or', 'xor'))) {
      $this->convertHavingFragment($havings['arg_1'], $values, $level);
      $this->convertHavingFragment($havings['arg_2'], $values, $level);
    }
    // Convert single conditional fragment.
    else {
      $this->convertHavingFragment($havings, $values, $level);
    }

    if ($closing_parenthesis) {
      $this->output[] = str_repeat("  ", $level - 1) . ")";
    }
  }

  /**
   * Transform having fragment into conditional object string.
   *
   * @param $havings
   *   Parsed having object.
   * @param $values
   *   Placeholder values.
   * @param $level
   *   Level of conditional statements.
   *
   * @return
   *   Array of having object string.
   *
   * @todo Refactor with convertConditionalFragment
   */
  protected function convertHavingFragment($fragment, &$values = array(), $level = 1) {
    // If arg_1 or arg_2 is a function, then we have to use the having() function.
    if ($fragment['arg_1']['type'] == 'function' || $fragment['arg_2']['type'] == 'function') {
      $output = str_repeat("  ", $level) . '->having("';
      $output .= $this->formatArg($fragment['arg_1']);
      $output .= ' ' . $fragment['op'] . ' ';
      $output .= $this->formatArg($fragment['arg_2']);
      $output .= '")';
    }
    else {
      $output = str_repeat("  ", $level) . "->havingCondition('" . $fragment['arg_1']['value'];
      if ($fragment['arg_2']['type'] == 'placeholder') {
        $value = array_shift($values);

        // IN operator now just takes an array, rather than imploded array.
        if ($fragment['op'] == 'in' && preg_match("/db_placeholders\(([^,]*).*\)/", $value, $matches)) {
          $value = $matches[1];
        }
      }
      else {
        $value = "'" . $fragment['arg_2']['value'] . "'";
      }
      $output .=  "', $value";

      // Add operator if necessary.
      if ($fragment['op'] != '=') {
        $output .= ", '" . $fragment['op'] . "'";
      }

      $output .= ")";
    }
    $this->output[] = $output;
  }

  /**
   * Get a table alias.
   *
   * If a table alias isn't already set, then generate one.
   *
   * @param $table_key
   *   The key of the table in the parsed sql array.
   *
   * @return
   *   The alias of the table name.
   */
  function getTableAlias($table_key) {
    if (!($alias = $this->sql_object->tables[$table_key]->alias)) {
      $alias = $this->generateTableAlias($table_key);
    }
    return $alias;
  }

  /**
   * Generate table alias.
   *
   * Usually the first letter of each word of the table
   * name (e.g. `n` for * `node` or `nr` for `node_revisions`).
   * If an alias is taken, then increments alias.
   *
   * @param $table_key
   *   The key of the table in the parsed sql array.
   *
   * @return
   *   The alias of the table name.
   */
  protected function generateTableAlias($table_key) {
    // Generate new table alias based on table name.
    $table_name = $this->sql_object->tables[0]->name;
    $table_words = explode('_', $table_name);
    $alias = '';
    foreach ($table_words as $table_word) {
      $alias .= $table_word[0];
    }

    // Get all current table aliases
    $table_aliases = array();
    foreach ($this->sql_object->tables as $table) {
      if ($table->alias) {
        $table_aliases[] = $table->alias;
      }
    }

    // Check if table alias already exists
    $original_alias = $alias;
    $i = 0;
    while (in_array($alias, $table_aliases)) {
      $alias = $original_alias . ++$i;
    }

    // Set table alias to generated alias
    $this->sql_object->tables[$table_key]->setAlias($alias);
    return $alias;
  }

  /**
   * Format field or argument into a string.
   *
   * Tests if a field or argument is a function, and if so, flattens function
   * into a string.
   *
   * @param $arg
   *   The parsed arg array to format.
   *
   * @return
   *   The formatted string.
   */
  function formatArg($arg) {
    if ($arg['type'] == 'function') {
      $output = $arg['value']['name'] . "(" . join(', ', $arg['value']['arg']) . ")";
    }
    else {
      $output = $arg['value'];
    }
    return $output;
  }
}

/**
 * SQL String Converter
 *
 * Converts Drupal 6 SQL strings into Drupal 7 SQL strings.  Replaces Drupal 6 
 * style placeholders (e.g. %d) with Drupal 7 style placeholders (e.g. :nid).
 */
class SqlStringConverter extends SqlConverter {

  /**
   * Performs the string conversion.
   */
  public function convert() {
    // Search for placeholders and convert them to Drupal 7 style placeholders.
    $this->convertConditionalPlaceholders($this->sql_object->conditional);

    return (string) $this->sql_object;
  }

  /**
   * Returns the SQL statement placeholders.
   *
   * @return
   *   An array of placeholders.
   */
  public function getPlaceholders() {
    return $this->placeholders;
  }
}
