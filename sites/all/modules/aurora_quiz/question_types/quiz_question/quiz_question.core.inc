<?php

/**
	* Classes used in the Quiz Question module.
	*
	* The core of the Quiz Question module is a set of abstract classes that
	* can be used to quickly and efficiently create new question types.
	*
	* Why OO?
	* Drupal has a long history of avoiding many of the traditional OO structures
	* and metaphors. However, with PHP 5, there are many good reasons to use OO
	* principles more broadly.
	*
	* The case for Quiz question types is that question types all share common
	* structure and logic. Using the standard hook-only Drupal metaphor, we are
	* forced to copy and paste large amounts of repetitive code from question
	* type to question type. By using OO principles and construction, we can
	* easily encapsulate much of that logic, while still making it easy to
	* extend the existing content.
	*
	* Where do I start?
	* To create a new question type, check out the multichoice question type for instance.
	*
	* @file
	*/

/**
	* A base implementation of a quiz_question, adding a layer of abstraction between the
	* node API, quiz API and the question types.
	*
	* It is required that Question types extend this abstract class.
	*
	* This class has default behaviour that all question types must have. It also handles the node API, but
	* gives the question types oppurtunity to save, delete and provide data specific to the question types.
	*
	* This abstract class also declares several abstract functions forcing question-types to implement required
	* methods.
	*/
abstract class QuizQuestion {
	/*
	* QUESTION IMPLEMENTATION FUNCTIONS
	*
	* This part acts as a contract(/interface) between the question-types and the rest of the system.
	*
	* Question types are made by extending these generic methods and abstract methods.
	*/

	/**
	* The current node for this question.
	*/
	public $node = NULL;
	// Extra node properties
	public $nodeProperties = NULL;
	public $annotation = '';

	/**
	* QuizQuestion constructor stores the node object.
	*
	* @param $node
	*   The node object
	*/
	public function __construct(stdClass &$node) {
		$this->node = $node;
	}

	/**
	* Allow question types to override the body field title
	*
	* @return
	*  The title for the body field
	*/
	public function getBodyFieldTitle() {
		return t('Question');
	}

	/**
	* Returns a node form to quiz_question_form
	*
	* Adds default form elements, and fetches question type specific elements from their
	* implementation of getCreationForm
	*
	* @param array $form_state
	* @return unknown_type
	*/
	public function getNodeForm(array &$form_state = NULL) {
		$form = array();

		// Mark this form to be processed by quiz_form_alter. quiz_form_alter will among other things
		// hide the revion fieldset if the user don't have permission to control the revisioning manually.
		$form['#quiz_check_revision_access'] = TRUE;

		// Allow user to set title?
		if (user_access('edit question titles')) {
			$form['helper']['#theme'] = 'quiz_question_creation_form';
			$form['title'] = array(
				'#type' => 'textfield',
				'#title' => t('Title'),
				'#maxlength' => 255,
				'#default_value' => $this->node->title,
				'#required' => FALSE,
				'#description' => t('Add a title that will help distinguish this question from other questions. This will not be seen during the @quiz.', array('@quiz' => QUIZ_NAME)),
			);
		} else {
			$form['title'] = array(
				'#type' => 'value',
				'#value' => $this->node->title,
			);
		}

		// Don't uncomment this here. Plug this code into each question node you need it for
		//$form['annotation'] = array(
		//    '#type' => 'textarea',
		//    '#title' => 'Question annotation',
		//    '#default_value' => $this->annotation,
		//    '#required' => FALSE,
		//    '#description' => t('Add an annotation to the question that can be used for grading or comparison by the user.'),
		//);

		// Store quiz id in the form
		$form['quiz_nid'] = array(
			'#type' => 'hidden',
		);
		$form['quiz_vid'] = array(
			'#type' => 'hidden',
		);

		if (isset($_GET['quiz_nid']) && isset($_GET['quiz_vid'])) {
			$form['quiz_nid']['#value'] = intval($_GET['quiz_nid']);
			$form['quiz_vid']['#value'] = intval($_GET['quiz_vid']);
		}

		// Identify this node as a quiz question type so that it can be recognized by other modules effectively.
		$form['is_quiz_question'] = array(
			'#type' => 'value',
			'#value' => TRUE
		);

		if (!empty($this->node->nid)) {
			if ($properties = entity_load('quiz_question', FALSE, array('nid' => $this->node->nid, 'vid' => $this->node->vid))) {
				$quiz_question = reset($properties);
			}
		}

		// Calls getCreationForm() for all of the subclasses and merges their custom elements with this form
		$form = array_merge($form, $this->getCreationForm($form_state));

		if ($this->hasBeenAnswered()) {
			$log = t('The current revision has been answered. We create a new revision so that the reports from the existing answers stays correct.');
			$this->node->revision = 1;
			$this->node->log = $log;
		}

		return $form;
	}

	/**
	* Retrieve information relevant for viewing the node.
	*
	* (This data is generally added to the node's extra field.)
	*
	* @return
	*  Content array
	*/
	public function getNodeView() {}

	/**
	* Getter function returning properties to be loaded when the node is loaded.
	*
	* @see load hook in quiz_question.module (quiz_question_load)
	*
	* @return array
	*/
	public function getNodeProperties() {
		if (isset($this->nodeProperties)) {
			return $this->nodeProperties;
		}

		//$props['max_score'] = db_query('SELECT max_score FROM {quiz_question_properties} WHERE nid = :nid AND vid = :vid', array(':nid' => $this->node->nid, ':vid' => $this->node->vid))->fetchField();
		$props['is_quiz_question'] = TRUE;
		$props['annotation'] = db_query('SELECT annotation FROM {quiz_question_properties} WHERE nid = :nid AND vid = :vid', array(':nid' => $this->node->nid, ':vid' => $this->node->vid))->fetchField();
		$this->nodeProperties = $props;

		return $props;
	}

	/**
	* Responsible for handling insert/update of question-specific data.
	* This is typically called from within the Node API, so there is no need
	* to save the node.
	*
	* The $is_new flag is set to TRUE whenever the node is being initially
	* created.
	*
	* A save function is required to handle the following three situations:
	* - A new node is created ($is_new is TRUE)
	* - A new node *revision* is created ($is_new is NOT set, because the
	*   node itself is not new).
	* - An existing node revision is modified.
	*
	* @see hook_update and hook_insert in quiz_question.module
	*
	* @param $is_new
	*  TRUE when the node is initially created.
	*/
	public function save($is_new = FALSE) {
		// We call the abstract function saveNodeProperties to save type specific data
		$this->saveNodeProperties($is_new);

		db_merge('quiz_question_properties')
			->key(array(
				'nid' => $this->node->nid,
				'vid' => $this->node->vid,
			))
			->fields(array(
				'nid' => $this->node->nid,
				'vid' => $this->node->vid,
				'max_score' => $this->getMaximumScore(),
				'annotation' => !empty($this->node->annotation['value']) ? $this->node->annotation['value'] : '',
				'annotation_format' => 'full_html',
			))
			->execute();

		// Save what quizzes this question belongs to.
		$quizzes_kept = $this->saveRelationships();
		if ($quizzes_kept && $this->node->revision) {
			if (user_access('manual quiz revisioning') && !variable_get('quiz_auto_revisioning', 1)) {
				unset($_GET['destination']);
				unset($_REQUEST['edit']['destination']);
				drupal_goto('quiz_question/' . $this->node->nid . '/' . $this->node->vid . '/revision_actions');
			} else {
				// For users without the 'manual quiz revisioning' permission we submit the revision_actions form silently with its default values set.
				$form_state = array();
				$form_state['values']['op'] = t('Submit');
				require_once DRUPAL_ROOT . '/' . drupal_get_path('module', 'quiz_question') . '/quiz_question.pages.inc';
				drupal_form_submit('quiz_question_revision_actions', $form_state, $this->node->nid, $this->node->vid);
			}
		}
	}

	/**
	* Delete question data from the database.
	*
	* Called by quiz_question_delete (hook_delete).
	* Child classes must call super
	*
	* @param $only_this_version
	*  If the $only_this_version flag is TRUE, then only the particular
	*  nid/vid combo should be deleted. Otherwise, all questions with the
	*  current nid can be deleted.
	*/
	public function delete($only_this_version = FALSE) {
		// Delete answeres
		$delete = db_delete('quiz_node_results_answers')
			->condition('question_nid', $this->node->nid);

		if ($only_this_version) {
			$delete->condition('question_vid', $this->node->vid);
		}

		$delete->execute();

		// Delete properties
		$delete = db_delete('quiz_question_properties')
			->condition('nid', $this->node->nid);

		if ($only_this_version) {
			$delete->condition('vid', $this->node->vid);
		}

		$delete->execute();
	}

	/**
	* Provides validation for question before it is created.
	*
	* When a new question is created and initially submited, this is
	* called to validate that the settings are acceptible.
	*
	* @param $form
	*  The processed form.
	*/
	abstract public function validateNode(array &$form);

	/**
	* Get the form through which the user will answer the question.
	*
	* @param $form_state
	*  The FAPI form_state array
	* @param $result_id
	*  The result id.
	* @return
	*  Must return a FAPI array.
	*/
	public function getAnsweringForm(array $form_state = NULL, $result_id) {
		$form = array();
		$form['#element_validate'] = array('quiz_question_element_validate');

		return $form;
	}

	/**
	* Get the form used to create a new question.
	*
	* @param
	*  FAPI form state
	* @return
	*  Must return a FAPI array.
	*/
	abstract public function getCreationForm(array &$form_state = NULL);

	/**
	* Get the maximum possible score for this question.
	*/
	abstract public function getMaximumScore();

	/**
	* Save question type specific node properties
	*/
	abstract public function saveNodeProperties($is_new = FALSE);

	/**
	* Save this Question to the specified Quiz.
	*/
	function saveRelationships() {
		if (empty($this->node->quiz_nid) || empty($this->node->quiz_vid)) {
			return;
		}

        if(USING__UTILITIES_LOG){
            $log = \lillockey\Utilities\App\InstanceHolder::log();
            $log->write_to_log("quiz_question.core.inc\t:saveRelationships");
        }else{
			$log = null;
		}

		$quiz_node = node_load($this->node->quiz_nid, $this->node->quiz_vid);

		if (quiz_has_been_answered($quiz_node)) {
			// We need to revise the quiz node if it has been answered
			$quiz_node->revision = 1;
			$quiz_node->auto_created = TRUE;
			node_save($quiz_node);
			drupal_set_message(t('New revision has been created for the @quiz %n', array('%n' => $quiz_node->title, '@quiz' => QUIZ_NAME)));
		}


        //krumo($this); die;
		$insert_values = array();
		$insert_values['parent_nid'] = $quiz_node->nid;
		$insert_values['parent_vid'] = $quiz_node->vid;
		$insert_values['child_nid'] = $this->node->nid;
		$insert_values['child_vid'] = $this->node->vid;
		$insert_values['max_score'] = $this->getMaximumScore();
		$insert_values['auto_update_max_score'] = $this->autoUpdateMaxScore() ? 1 : 0;
		$insert_values['weight'] = 1 + db_query('SELECT MAX(weight) FROM quiz_node_relationship WHERE parent_vid = :vid',
				array(':vid' => $quiz_node->vid))->fetchField();
		$rnd_query = "SELECT randomization FROM quiz_node_properties WHERE nid = :nid AND vid = :vid";
		$randomization =
			db_query($rnd_query,
				array(':nid' => $quiz_node->nid, ':vid' => $quiz_node->vid))->fetchField();
		$insert_values['question_status'] = $randomization == 2 ? QUIZ_QUESTION_RANDOM : QUIZ_QUESTION_ALWAYS;
        if(USING__UTILITIES_LOG)
			$log->write_to_log("quiz_question.core.inc\t:\tFiring entity_create() where child_nid = {$insert_values['child_nid']} and child_vid = {$insert_values['child_vid']}");


        $db = \lillockey\Utilities\App\InstanceHolder::db();
        $count = $db->count('quiz_node_relationship', array(
            'parent_nid' => $insert_values['parent_nid'],
            'parent_vid' => $insert_values['parent_vid'],
            'child_nid' => $insert_values['child_nid'],
        ));
        if($count == 0){
            /**
             * @var QuizQuestionRelationship $entity
             */

            $child_version = $db->select_one_value_by('node', 'vid', array('nid' => $this->node->nid));
            if($child_version && $child_version != $insert_values['child_vid'])
                $insert_values['child_vid'] = $child_version;
            $entity = entity_create('quiz_question_relationship', $insert_values);
            $entity->save();
        }


		//entity_create('quiz_question_relationship', $insert_values)->save();

		// Update max_score for relationships if auto update max score is enabled for question
		$quizzes_to_update = array();
		$result = db_query('SELECT parent_vid as vid from {quiz_node_relationship} where child_nid = :nid and child_vid = :vid and auto_update_max_score=1', array(':nid' => $this->node->nid, ':vid' => $this->node->vid));

		foreach ($result as $record) {
			$quizzes_to_update[] = $record->vid;
		}

		db_update('quiz_node_relationship')
			->fields(array('max_score' => $this->getMaximumScore()))
			->condition('child_nid', $this->node->nid)
			->condition('child_vid', $this->node->vid)
			->condition('auto_update_max_score', 1)
			->execute();

		quiz_update_max_score_properties($quizzes_to_update);
		quiz_update_max_score_properties(array($quiz_node->vid));
	}

	/**
	* Finds out if a question has been answered or not
	*
	* This function also returns TRUE if a quiz that this question belongs to have been answered.
	* Even if the question itself haven't been answered. This is because the question might have
	* been rendered and a user is about to answer it...
	*
	* @return
	*   true if question has been answered or is about to be answered...
	*/
	public function hasBeenAnswered() {
		if (!isset($this->node->vid)) {
			return FALSE;
		}

		$answered = db_query_range('SELECT 1 FROM {quiz_node_results} qnres JOIN {quiz_node_relationship} qnrel ON (qnres.vid = qnrel.parent_vid) WHERE qnrel.child_vid = :child_vid', 0, 1, array(':child_vid' => $this->node->vid))->fetch();

		return $answered ? TRUE : FALSE;
	}

	/**
	* Determines if the user can view the correct answers
	*
	* @todo grabbing the node context here probably isn't a great idea
	*
	* @return boolean
	*   true iff the view may include the correct answers to the question
	*/
	public function viewCanRevealCorrect() {
		//global $user;
		$quiz_node = node_load(arg(1));
		$reveal_correct[] = user_access('view any quiz question correct response');
		//$reveal_correct[] = ($user->uid == $this->node->uid);

		if (array_filter($reveal_correct)) {
			return TRUE;
		}
	}

	/**
	* Utility function that returns the format of the node body
	*/
	protected function getFormat() {
		$node = isset($this->node) ? $this->node : $this->question;
		$body = field_get_items('node', $node, 'body');

		return isset($body[0]['format']) ? $body[0]['format'] : NULL;
	}

	/**
	* This may be overridden in subclasses. If it returns true,
	* it means the max_score is updated for all occurrences of
	* this question in quizzes.
	*/
	protected function autoUpdateMaxScore() {
		return false;
	}

	public function getAnsweringFormValidate(array &$form, array &$form_state = NULL) {}

	/**
	* Is this question graded?
	*
	* Questions like Quiz Directions, Quiz Page, and Scale are not.
	*
	* By default, questions are expected to be gradeable
	*
	* @return bool
	*/
	public function isGraded() {
		return TRUE;
	}

  /**
   * Does this question type give feedback?
   *
   * Questions like Quiz Directions and Quiz Pages do not.
   *
   * By default, questions give feedback
   *
   * @return bool
   */
  public function hasFeedback() {
    return TRUE;
  }

}

/**
	* Each question type must store its own response data and be able to calculate a score for
	* that data.
	*/
abstract class QuizQuestionResponse {
	// Result id
	protected $result_id = 0;
	protected $is_correct = FALSE;
	protected $evaluated = TRUE;
	// The question node(not a quiz question instance)
	public $question = NULL;
	public $quizQuestion = NULL;
	protected $answer = NULL;
	protected $score;
	public $is_skipped;
	public $is_doubtful;

	/**
	* Create a new user response.
	*
	* @param $result_id
	*  The result ID for the user's result set. There is one result ID per time
	*  the user takes a quiz.
	* @param $question_node
	*  The question node.
	* @param $answer
	*  The answer (dependent on question type).
	*/
	public function __construct($result_id, stdClass $question_node, $answer = NULL) {
		$this->result_id = $result_id;
		$this->question = $question_node;
		$this->quizQuestion = _quiz_question_get_instance($question_node);
		$this->answer = $answer;
		$result = db_query('SELECT is_skipped, is_doubtful FROM {quiz_node_results_answers} WHERE result_id = :result_id AND question_nid = :question_nid AND question_vid = :question_vid', array(':result_id' => $result_id, ':question_nid' => $question_node->nid, ':question_vid' => $question_node->vid))->fetch();

		if (is_object($result)) {
			$this->is_doubtful = $result->is_doubtful;
			$this->is_skipped = $result->is_skipped;
		}
	}

	/**
	*
	* @return QuizQuestion
	*/
	function getQuizQuestion() {
		return $this->quizQuestion;
	}

	/**
	* Used to refresh this instances question node in case drupal has changed it.
	*
	* @param $newNode
	*  Question node
	*/
	public function refreshQuestionNode($newNode) {
		$this->question = $newNode;
	}

	/**
	* Indicate whether the response has been evaluated (scored) yet.
	* Questions that require human scoring (e.g. essays) may need to manually
	* toggle this.
	*/
	public function isEvaluated() {
		return (bool) $this->evaluated;
	}

	/**
	* Check to see if the answer is marked as correct.
	*
	* This default version returns TRUE iff the score is equal to the maximum possible score.
	*/
	function isCorrect() {
		return ($this->getMaxScore() == $this->getScore());
	}

	/**
	* Returns stored score if it exists, if not the score is calculated and returned.
	*
	* @param $weight_adjusted
	*  If the returned score shall be adjusted according to the max_score the question has in a quiz
	* @return
	*  Score(int)
	*/
	function getScore($weight_adjusted = TRUE) {
		if ($this->is_skipped) {
			return 0;
		}

		//if (!isset($this->score)) {
		//    $this->score = $this->score();
		//}

		//if (isset($this->question->score_weight) && $weight_adjusted) {
		//    return round($this->score * $this->question->score_weight);
		//}

		return $this->score();
	}

	/**
	* Returns stored max score if it exists, if not the max score is calculated and returned.
	*
	* @param $weight_adjusted
	*  If the returned max score shall be adjusted according to the max_score the question has in a quiz
	* @return
	*  Max score(int)
	*/
	public function getMaxScore($weight_adjusted = TRUE) {
		//if (!isset($this->question->max_score)) {
		//    $this->question->max_score = $this->question->getMaximumScore();
		//}

		//if (isset($this->question->score_weight) && $weight_adjusted) {
		//    return round($this->question->max_score * $this->question->score_weight);
		//}

		return _quiz_question_get_instance($this->question)->getMaximumScore();
	}

	/**
	* Represent the response as a stdClass object.
	*
	* Convert data to an object that has the following properties:
	* - $score
	* - $result_id
	* - $nid
	* - $vid
	* - $is_correct
	*/
	function toBareObject() {
		$obj = new stdClass();
		$obj->score = $this->getScore(); // This can be 0 for unscored.
		$obj->nid = $this->question->nid;
		$obj->vid = $this->question->vid;
		$obj->result_id = $this->result_id;
		$obj->is_correct = (int) $this->isCorrect();
		$obj->is_evaluated = $this->isEvaluated();
		$obj->is_skipped = 0;
		$obj->is_doubtful = isset($_POST['is_doubtful']) ? $_POST['is_doubtful'] : 0;
		$obj->is_valid = $this->isValid();

		return $obj;
	}

	/**
	* Validates response from a quiz taker. If the response isn't valid the quiz taker won't be allowed to proceed.
	*
	* @return
	*  True if the response is valid.
	*  False otherwise
	*/
	public function isValid() {
		return TRUE;
	}

	/**
	* Get data suitable for reporting a user's score on the question.
	* This expects an object with the following attributes:
	*
	*  answer_id; // The answer ID
	*  answer; // The full text of the answer
	*  is_evaluated; // 0 if the question has not been evaluated, 1 if it has
	*  score; // The score the evaluator gave the user; this should be 0 if is_evaluated is 0.
	*  question_vid
	*  question_nid
	*  result_id
	*/
	public function getReport() {
	// Basically, we encode internal information in a legacy array format for Quiz.
		$report = array(
			'answer_id' => 0, // <-- Stupid vestige of multichoice.
			'answer' => $this->answer,
			'is_evaluated' => $this->isEvaluated(),
			'is_correct' => $this->isCorrect(),
			'score' => $this->getScore(),
			'question_vid' => $this->question->vid,
			'question_nid' => $this->question->nid,
			'result_id' => $this->result_id,
		);

		//watchdog('REPORT', '$report = %report', array('%report' => print_r($report, TRUE)));

		return $report;
	}

	/**
	* Creates the report form for the admin pages, and for when a user gets feedback after answering questions.
	*
	* The report is a form to allow editing scores and the likes while viewing the report form
	*
	* @return $form
	*  Drupal form array
	*/
	public function getReportForm() {
		// Add general data, and data from the question type implementation
		//watchdog('quiz_question.core.inc - getReportForm()', '$this =  %view', array('%view' => dprint_r($this, TRUE)));
		$form = array();

		$form['nid'] = array(
			'#type' => 'value',
			'#value' => $this->question->nid,
		);

		$form['vid'] = array(
			'#type' => 'value',
			'#value' => $this->question->vid,
		);

		$form['result_id'] = array(
			'#type' => 'value',
			'#value' => $this->result_id,
		);

		if (quiz_access_to_score() && $submit = $this->getReportFormSubmit()) {
			$form['submit'] = array(
				'#type' => 'value',
				'#value' => $submit,
			);
		}

		$form['question'] = $this->getReportFormQuestion();

		//if (quiz_access_to_score()) {
		//    $form['answer_annotation'] = $this->getReportFormAnswerAnnotation();
		//}

		$form['max_score'] = array(
			'#type' => 'value',
			'#value' => ($this->canReview('score')) ? $this->getMaxScore() : '?',
		);

		$rows = array();

		$labels = array(
			'attempt' => t(''),
			'choice' => t('Options:'),
			//'correct' => t('Correct?'),
			//'solution' => t('The correct answer:'),
			'score' => t('Score'),
			'question_annotation' => t('Question annotation:'),
		);

		drupal_alter('quiz_feedback_labels', $labels);

		foreach ($this->getReportFormResponse() as $idx => $row) {
			foreach ($labels as $reviewType => $label) {
				if (isset($row[$reviewType])) {
					$rows[$idx][$reviewType] = $row[$reviewType];
				}
			}
		}

		if ($this->isEvaluated()) {
			$score = $this->getScore();

			if ($this->isCorrect()) {
				$class = 'q-correct';
			} else {
				$class = 'q-wrong';
			}
		} else {
			$score = t('?');
			$class = 'q-waiting';
		}

		//if (quiz_access_to_score() && $submit) {
		//    $form['score'] = $this->getReportFormScore();
		//}

		if ($this->canReview('score') || quiz_access_to_score()) {
			$form['score_display']['#markup'] = theme('quiz_question_score', array('score' => $score, 'max_score' => $this->getMaxScore(), 'class' => $class));
		}

		if (isset($rows) && is_array($rows) && isset($rows[0])) {
			$headers = array_intersect_key($labels, $rows[0]);
		}

		$type = $this->getQuizQuestion()->node->type;
        if(!isset($headers)) $headers = array();
		$form['response']['#markup'] = theme('quiz_question_annotation__' . $type, array('labels' => $headers, 'data' => $rows));

		if ($this->canReview('question_annotation')) {
			if ($properties = entity_load('quiz_question', FALSE, array('nid' => $this->quizQuestion->node->nid, 'vid' => $this->quizQuestion->node->vid))) {
				$quiz_question = reset($properties);
				$form['question_annotation']['#markup'] = check_markup($quiz_question->annotation, $quiz_question->annotation_format);
			}
		}

		//watchdog('quiz_question.core.inc - getReportForm()', '%view', array('%view' => '$form = ' . dprint_r($form, TRUE)));

		$form['#theme'] = $this->getReportFormTheme();

		return $form;
	}

	/**
	* get the question part of the reportForm
	*
	* @return
	*  FAPI form array holding the question
	*/
	public function getReportFormQuestion() {
		$node = node_load($this->question->nid);
		$node->no_answer_form = TRUE;
		node_build_content($node, 'question');

		return $node->content;
	}

	/**
	* Get the response part of the report form

	* @return
	*  Array of choices
	*/
	public function getReportFormResponse() {
		$data = array();

		$data[] = array(
			'choice' => 'True',
			'attempt' => 'Did the user choose this?',
			//'correct' => 'Was their answer correct?',
			'score' => 'Points earned for this answer',
			'question_annotation' => 'General question annotation for any answer',
			'solution' => 'Is this choice the correct solution?',
		);

		return $data;
	}

	public function getReportFormAnswerAnnotation() {
		return FALSE;
	}

	/**
	* Get the submit function for the reportForm
	*
	* @return
	*  Submit function as a string, or FALSE if no submit function
	*/
	public function getReportFormSubmit() {
		return FALSE;
	}

	/**
	* Get the validate function for the reportForm
	*
	* @return
	*  Validate function as a string, or FALSE if no validate function
	*/
	public function getReportFormValidate(&$element, &$form_state) {
		return FALSE;
	}

	/**
	* Get the theme key for the reportForm
	*
	* @return
	*  Theme key as a string, or FALSE if no submit function
	*/
	public function getReportFormTheme() {
		return FALSE;
	}

	/**
	* Saves the quiz result. This should only be called when an answer is
	* provided.
	*/
	public function saveResult() {
		$this->save();
	}

	/**
	* Utility function that returns the format of the node body
	*/
	protected function getFormat() {
		$body = field_get_items('node', $this->question, 'body');

		return ($body ? $body[0]['format'] : NULL);
	}

	/**
	* Save the current response.
	*/
	abstract public function save();

	/**
	* Delete the response.
	*/
	abstract public function delete();

	/**
	* Calculate the score for the response.
	*/
	abstract public function score();

	/**
	* Get the user's response.
	*/
	abstract public function getResponse();

	/**
	* Can the quiz taker view the requested review?
	*/
	public function canReview($option) {
		$can_review = &drupal_static(__METHOD__, array());

		if (!isset($can_review[$option])) {
			$quiz_result = quiz_result_load($this->result_id);
			$can_review[$option] = quiz_feedback_can_review($option, $quiz_result);
		}

		return $can_review[$option];
	}

	/**
	* Implementation of getReportFormScore
	*
	* @see QuizQuestionResponse#getReportFormScore()
	*/
	public function getReportFormScore() {
		$score = ($this->isEvaluated()) ? $this->getScore() : '';

		return array(
			'#title' => 'Enter score',
			'#type' => 'textfield',
			'#default_value' => $score,
			'#size' => 3,
			'#maxlength' => 3,
			'#attributes' => array('class' => array('quiz-report-score')),
			'#element_validate' => array('element_validate_integer'),
			'#required' => TRUE,
			'#field_suffix' => '/ ' . $this->getMaxScore(),
		);
	}

	/**
	* Set the target result ID for this Question response.
	*
	* Useful for cloning entire result sets.
	*/
	public function setResultId($result_id) {
		$this->result_id = $result_id;
	}
}
