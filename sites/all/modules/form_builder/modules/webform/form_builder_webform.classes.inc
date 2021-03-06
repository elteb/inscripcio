<?php


class FormBuilderWebformForm extends FormBuilderFormBase {
  /**
   * {@inheritdoc}
   */
  public static function loadFromStorage($form_type, $form_id, $sid, $params) {
    // Webform identifies its forms by Node Id.
    $node = node_load($form_id);
    $form = new static($form_type, $form_id, $sid, $params, array());
    $form->addComponents($node->webform['components']);
    drupal_alter('form_builder_load', $form, $form_type, $form_id);
    return $form;
  }

  /**
   * Add components to the form.
   *
   * @param array $components
   *   A components array as you would find it in $node->webform['components'].
   */
  public function addComponents($components) {
    foreach ($components as $cid => $component) {
      $element['#webform_component'] = $component;
      $element['#weight'] = $component['weight'];
      $element['#key'] = $component['form_key'];
      $parent_id = $component['pid'] ? 'cid_' . $component['pid'] : FORM_BUILDER_ROOT;
      $element['#form_builder'] = array(
        'element_id' => 'cid_' . $cid,
        'parent_id' => $parent_id,
      );
      if ($map = _form_builder_webform_property_map($component['type'])) {
        // Keep the internal type of this element as the component type. For example
        // this may match an $element['#type'] of 'webform_date' and set the
        // $element['#form_builder']['element_type'] to simply 'date'.
        if (isset($map['form_builder_type'])) {
          $element['#form_builder']['element_type'] = $map['form_builder_type'];
        }
      }
      if ($e = form_builder_webform_component_invoke($component['type'], 'form_builder_load', $element)) {
        $element = $e;
      }
      $this->setElementArray($element, $parent_id);
    }
  }

  /**
   * Create a webform component array based the form_builder cache.
   *
   * @param string $element_id
   *   Unique ID of the element.
   * @return array
   *   A webform component array.
   */
  public function getComponent($element_id) {
    module_load_include('inc', 'form_builder_webform', 'form_builder_webform.components');

    $element = $this->getElementArray($element_id);
    $component = $element['#webform_component'];
    $type = $component['type'];

    $component['email'] = 0;
    $component['nid'] = $this->formId;
    $component['weight'] = $element['#weight'];
    // The component can't decide this on it's own.
    $component['pid'] = 0;

    // Allow each component to set any specific settings that can't be mapped.
    if ($saved_component = form_builder_webform_component_invoke($type, 'form_builder_save', $component, $element)) {
      $component = $saved_component;
    }

    return $component;
  }

  /**
   * Get a list of component-arrays just like in $node->webform['components'].
   */
  public function getComponents($node) {
    // Max CID is used in the creation of new components, preventing conflicts.
    $cids = array();
    // Filter out all cids from removed components.
    foreach (array_keys($node->webform['components']) as $cid) {
      if ($this->getElement("cid_$cid")) {
        $cids[] = $cid;
      }
    }
    $max_cid = !empty($cids) ? max($cids) : 0;

    $components = array();
    // Keep track of the element_id => cid mapping for assigning the pid.
    $element_cid_map = array();
    $page = 1;
    foreach ($this->getElementsInPreOrder() as $element_id => $element) {
      if ($component = $this->getComponent($element_id)) {
        if (empty($component['cid'])) {
          $component['cid'] = ++$max_cid;
        }
        $element_cid_map[$element_id] = $component['cid'];

        // Set the possibly changed pid.
        $parent_id = $element->parentId();
        $component['pid'] = ($parent_id === FORM_BUILDER_ROOT) ? 0 : $element_cid_map[$parent_id];
        $component['page_num'] = $page;

        $components[$component['cid']] = $component;
        if ($component['type'] == 'pagebreak') {
          $page++;
        }
      }
    }
    return $components;
  }
}

class FormBuilderWebformElement extends FormBuilderElementBase {

  /**
   * {@inheritdoc}
   */
  protected function setProperty($property, $value) {
    $component = &$this->element['#webform_component'];
    $properties = $this->getProperties();
    $properties[$property]->setValue($component, $value);
  }

  /**
   * {@inheritdoc}
   */
  public function render() {
    $element = $this->element;
    if (isset($element['#webform_component'])) {
      $component = $element['#webform_component'];
      $new_element = webform_component_invoke($component['type'], 'render', $component, NULL, FALSE);
      // Preserve the #weight. It may have been changed by the positions form.
      $new_element['#weight'] = $element['#weight'];
      $new_element['#key'] = $element['#key'];
      $new_element['#webform_component'] = $component;
      $new_element['#form_builder'] = $element['#form_builder'];
      return $this->addPreRender($new_element);
    }
    return $this->addPreRender($element);
  }

}

class FormBuilderWebformProperty extends FormBuilderPropertyBase {

  protected $storageParents;

  /**
   * {@inheritdoc}
   */
  public function __construct($property, $params, $form_type_name) {
    $params += array('storage_parents' => array($property));
    parent::__construct($property, $params, $form_type_name);
    $this->storageParents = $params['storage_parents'];
  }

  /**
   * {@inheritdoc}
   */
  public function setValue(&$component, $value) {
    drupal_array_set_nested_value($component, $this->storageParents, $value);
  }

  /**
   * {@inheritdoc}
   */
  public function form(&$form_state, $element) {
    // We use the rendered element here to re-use the form-API functions.
    $e = $element->render();
    $e += array("#{$this->property}" => $this->getValue($e['#webform_component']));
    if (isset($this->params['form']) && function_exists($this->params['form'])) {
      $function = $this->params['form'];
      return $function($form_state, $this->formTypeName, $e, $this->property);
    }
    return array();
  }

  /**
   * Read the value from a component array.
   */
  public function getValue($component) {
    return drupal_array_get_nested_value($component, $this->storageParents);
  }

}

/**
 * Special handling for the mandatory -> required rename in webform4.
 */
class FormBuilderWebformPropertyRequired extends FormBuilderWebformProperty {
  public function setValue(&$component, $value) {
    $component['required'] = $value; // webform 4
    $component['mandatory'] = $value; // webform 3
  }

}

/**
 * Special handling for $component['value'].
 */
class FormBuilderWebformPropertyValue extends FormBuilderWebformProperty {
  /**
   * {@inheritdoc}
   */
  public function setValue(&$component, $value) {
    if (is_array($value)) {
      $value = implode(',', $value);
    }
    parent::setValue($component, $value);
  }
}

/**
 * Special handling for $component['options'].
 */
class FormBuilderWebformPropertyOptions extends FormBuilderWebformProperty {
  public function submit($form, &$form_state) {
    // Webform needs the options to be saved as text.
    $options = $form_state['values']['options']['options_field'];
    parent::submit($form, $form_state);
    $form_state['values']['options'] = $options;
  }
}
