export default class S {
    Start() {
        const tr = this.gameObject.GetComponent("Transform");
        this.sameTransform = tr === this.transform;
        this.noBody = GetComponent("Rigidbody") === null;
        const other = GameObject.Find("Other");
        this.otherName = other ? other.name : "none";
        this.text = String(this.gameObject);
        Debug.Log("found " + this.otherName);
    }
}
