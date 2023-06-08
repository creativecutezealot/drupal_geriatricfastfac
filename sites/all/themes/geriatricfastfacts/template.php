<?php
/**
 * @file template.php
 */

function geriatricfastfacts_menu_tree__menu_utility_menu(&$variables) {
  return '<ul class="menu nav navbar-nav navbar-right">' . $variables['tree'] . '</ul>';
}

function geriatricfastfacts_preprocess_search_result(&$variables) {
	$variables['search_results'] = '';
	
	  if (!empty($variables['module'])) {
		$variables['module'] = check_plain($variables['module']);
	  }
	  if (isset($variables['results'])) {
		  foreach ($variables['results'] as $result) {
			$variables['search_results'] .= theme('search_result', array('result' => $result, 'module' => $variables['module']));
		  }
	  }

		  $variables['ff_num'] = $variables['result']['node']->field_fast_fact_number[LANGUAGE_NONE][0]['value'];
		  $variables['pager'] = theme('pager', array('tags' => NULL));
		  $variables['theme_hook_suggestions'][] = 'search_results__' . $variables['module'];
}